/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2017> <Stephan Gatzka>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cio_endian.h"
#include "cio_error_code.h"
#include "cio_random.h"
#include "cio_utf8_checker.h"
#include "cio_util.h"
#include "cio_websocket.h"
#include "cio_websocket_masking.h"

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

static const uint8_t WS_MASK_SET = 0x80;
static const uint8_t WS_HEADER_FIN = 0x80;
static const unsigned int WS_MID_FRAME_SIZE = 65535;

static const uint64_t close_timeout_ns = UINT64_C(10) * UINT64_C(1000) * UINT64_C(1000);

static void enqueue_job(struct cio_websocket *ws, struct cio_websocket_write_job *job)
{
	if (ws->first_write_job == NULL) {
		ws->first_write_job = job;
		ws->last_write_job = job;
	} else {
		ws->last_write_job->next = job;
		ws->last_write_job = job;
	}
}

static struct cio_websocket_write_job *dequeue_job(struct cio_websocket *ws)
{
	struct cio_websocket_write_job *job = ws->first_write_job;
	if (job == NULL) {
		return NULL;
	} else if (ws->first_write_job == ws->last_write_job) {
		ws->first_write_job = NULL;
	} else {
		ws->first_write_job = job->next;
	}

	return job;
}

static void abort_write_jobs(struct cio_websocket *ws)
{
	struct cio_websocket_write_job *job = dequeue_job(ws);

	while (job != NULL) {
		job->wbh = NULL;
		job->handler(ws, job->handler_context, CIO_OPERATION_ABORTED);
		job = dequeue_job(ws);
	}
}

static void close(struct cio_websocket *ws)
{
	if (likely(ws->read_handler != NULL)) {
		ws->read_handler(ws, ws->read_handler_context, CIO_EOF, NULL, 0, false, false);
	}

	if (ws->close_hook) {
		ws->close_hook(ws);
	}
}

static void add_websocket_header(struct cio_websocket_write_job *job)
{
	cio_write_buffer_queue_head(job->wbh, &job->websocket_header);
}

static void remove_websocket_header(const struct cio_websocket_write_job *job)
{
	cio_write_buffer_queue_dequeue(job->wbh);
}

static enum cio_error send_frame(struct cio_websocket *ws, struct cio_websocket_write_job *job)
{
	uint8_t first_len;
	size_t header_index = 2;

	size_t length = 0;
	struct cio_write_buffer *element = job->wbh;
	for (size_t i = 0; i < job->wbh->data.q_len; i++) {
		element = element->next;
		length += element->data.element.length;
	}

	uint8_t first_byte = (uint8_t)job->frame_type;
	if (job->last_frame) {
		first_byte |= WS_HEADER_FIN;
	}

	job->send_header[0] = first_byte;

	if (length <= CIO_WEBSOCKET_SMALL_FRAME_SIZE) {
		first_len = (uint8_t)length;
	} else if (length <= WS_MID_FRAME_SIZE) {
		uint16_t be_len = cio_htobe16((uint16_t)length);
		memcpy(&job->send_header[2], &be_len, sizeof(be_len));
		header_index += sizeof(be_len);
		first_len = CIO_WEBSOCKET_SMALL_FRAME_SIZE + 1;
	} else {
		uint64_t be_len = cio_htobe64((uint64_t)length);
		memcpy(&job->send_header[2], &be_len, sizeof(be_len));
		header_index += sizeof(be_len);
		first_len = CIO_WEBSOCKET_SMALL_FRAME_SIZE + 2;
	}

	if (ws->ws_flags.is_server == 0) {
		first_len |= WS_MASK_SET;
		uint8_t mask[4];
		cio_random_get_bytes(mask, sizeof(mask));
		memcpy(&job->send_header[header_index], &mask, sizeof(mask));
		header_index += sizeof(mask);
		// TODO: cio_websocket_mask(payload, length, mask );
	}

	job->send_header[1] = first_len;

	cio_write_buffer_element_init(&job->websocket_header, job->send_header, header_index);
	add_websocket_header(job);
	enqueue_job(ws, job);
	return ws->bs->write(ws->bs, job->wbh, job->stream_handler, ws);
}

static void prepare_close_job(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const uint8_t *reason, size_t reason_length, cio_websocket_write_handler handler, void *handler_context, cio_buffered_stream_write_handler stream_handler)
{
	size_t close_buffer_length = sizeof(status_code);
	status_code = cio_htobe16(status_code);
	memcpy(ws->close_payload_buffer, &status_code, sizeof(status_code));
	if (reason != NULL) {
		size_t copy_len = MIN(reason_length, sizeof(ws->close_payload_buffer) - sizeof(status_code));
		memcpy(ws->close_payload_buffer + sizeof(status_code), reason, copy_len);
		close_buffer_length += copy_len;
	}

	cio_write_buffer_head_init(&ws->wb_head_close_payload_buffer);
	cio_write_buffer_element_init(&ws->wb_close_payload_buffer, ws->close_payload_buffer, close_buffer_length);
	cio_write_buffer_queue_tail(&ws->wb_head_close_payload_buffer, &ws->wb_close_payload_buffer);
	ws->write_close_job.wbh = &ws->wb_head_close_payload_buffer;
	ws->write_close_job.handler = handler;
	ws->write_close_job.handler_context = handler_context;
	ws->write_close_job.frame_type = CIO_WEBSOCKET_CLOSE_FRAME;
	ws->write_close_job.last_frame = true;
	ws->write_close_job.stream_handler = stream_handler;
}

static void close_frame_written(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err);

static void handle_error(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const char *reason)
{
	if (ws->on_error != NULL) {
		ws->on_error(ws, status_code, reason);
	}

	prepare_close_job(ws, status_code, (const uint8_t *)reason, strlen(reason), NULL, NULL, close_frame_written);
	send_frame(ws, &ws->write_close_job);
	close(ws);
}

static inline struct cio_websocket_write_job *get_job(struct cio_websocket *ws)
{
	struct cio_websocket_write_job *job = dequeue_job(ws);
	remove_websocket_header(job);
	job->wbh = NULL;

	return job;
}

static void message_written(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	(void)bs;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	struct cio_websocket_write_job *job = get_job(ws);

	if (likely(job->handler != NULL)) {
		job->handler(ws, job->handler_context, err);
	}

	if (ws->first_write_job != NULL) {
		 err = send_frame(ws, ws->first_write_job);
		 if (unlikely(err != CIO_SUCCESS)) {
		 	handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "could not send next frame");
		 }
	}
}

static void response_close_frame_written(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	(void)bs;
	(void)err;

	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	get_job(ws);

	abort_write_jobs(ws);
	close(ws);
}

static void close_frame_written(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	(void)bs;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	struct cio_websocket_write_job *job = get_job(ws);

	abort_write_jobs(ws);

	if (job->handler) {
		job->handler(ws, job->handler_context, err);
	}
}

static bool is_invalid_status_code(uint16_t status_code)
{
	if ((status_code >= CIO_WEBSOCKET_CLOSE_NORMAL) && (status_code <= CIO_WEBSOCKET_CLOSE_UNSUPPORTED)) {
		return false;
	}

	if ((status_code >= CIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA) && (status_code <= CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR)) {
		return false;
	}

	if ((status_code >= CIO_WEBSOCKET_CLOSE_RESERVED_LOWER_BOUND) && (status_code <= CIO_WEBSOCKET_CLOSE_RESERVED_UPPER_BOUND)) {
		return false;
	}

	return true;
}

static void get_header(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer);

static void close_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;
	if (err != CIO_OPERATION_ABORTED) {
		struct cio_websocket *ws = (struct cio_websocket *)handler_context;
		ws->close_timer.close(&ws->close_timer);
		close(ws);
	}
}

static int payload_size_in_limit(const struct cio_write_buffer *payload, size_t limit)
{
	if (likely(payload != NULL)) {
		size_t payload_length = 0;

		const struct cio_write_buffer *element = payload->next;
		while (element != payload) {
			payload_length += element->data.element.length;
			element = element->next;
		}

		if (unlikely(payload_length > limit)) {
			return 0;
		}
	}

	return 1;
}

static void handle_binary_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length, bool last_frame)
{
	ws->read_handler(ws, ws->read_handler_context, CIO_SUCCESS, data, length, last_frame, true);
}

static void handle_text_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length, bool last_frame)
{
	enum cio_utf8_status status = cio_check_utf8(&ws->utf8_state, data, length);

	if (unlikely((status == CIO_UTF8_REJECT) || (last_frame && (status != CIO_UTF8_ACCEPT)))) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA, "payload not valid utf8");
		return;
	}

	ws->read_handler(ws, ws->read_handler_context, CIO_SUCCESS, data, length, last_frame, false);
	if (last_frame) {
		cio_utf8_init(&ws->utf8_state);
	}
}

static void handle_close_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	uint64_t len = length;

	if (unlikely(length == 1)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "close payload of length 1");
		return;
	}

	uint16_t status_code;
	if (length >= 2) {
		memcpy(&status_code, data, sizeof(status_code));
		status_code = cio_be16toh(status_code);
		if (unlikely(is_invalid_status_code(status_code))) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "invalid status code in close");
			return;
		}

		length -= sizeof(status_code);
	} else {
		status_code = CIO_WEBSOCKET_CLOSE_NORMAL;
	}

	if (length > 0) {
		struct cio_utf8_state state;
		cio_utf8_init(&state);
		if (unlikely(cio_check_utf8(&state, data + 2, length - 2) != CIO_UTF8_ACCEPT)) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA, "reason in close frame not utf8 valid");
			return;
		}
	}

	if (ws->on_control != NULL) {
		ws->on_control(ws, CIO_WEBSOCKET_CLOSE_FRAME, data, len);
	}

	if (ws->ws_flags.self_initiated_close == 1) {
		ws->close_timer.cancel(&ws->close_timer);
		ws->close_timer.close(&ws->close_timer);
		close(ws);
	} else {
		char *reason;
		if (length > 0) {
			reason = (char *)data + sizeof(status_code);
		} else {
			reason = NULL;
		}

		prepare_close_job(ws, status_code, (const uint8_t *)reason, length, NULL, NULL, response_close_frame_written);
		enum cio_error err = send_frame(ws, &ws->write_close_job);
		if (unlikely(err != CIO_SUCCESS)) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "could not send close frame");
		}
	}
}

static void handle_ping_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	cio_write_buffer_head_init(&ws->wb_head_ping_payload_buffer);
	if (length > 0) {
		memcpy(ws->ping_payload_buffer, data, length);
		cio_write_buffer_element_init(&ws->wb_ping_payload_buffer, ws->ping_payload_buffer, length);
		cio_write_buffer_queue_tail(&ws->wb_head_ping_payload_buffer, &ws->wb_ping_payload_buffer);
	}

	if (ws->on_control != NULL) {
		ws->on_control(ws, CIO_WEBSOCKET_PING_FRAME, data, length);
	}

	ws->write_pong_job.wbh = &ws->wb_head_ping_payload_buffer;
	ws->write_pong_job.handler = NULL;
	ws->write_pong_job.handler_context = NULL;
	ws->write_pong_job.frame_type = CIO_WEBSOCKET_PONG_FRAME;
	ws->write_pong_job.last_frame = true;
	ws->write_pong_job.stream_handler = message_written;

	enum cio_error err = send_frame(ws, &ws->write_pong_job);
	if (unlikely(err != CIO_SUCCESS)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "could not send close frame");
	} else {
		ws->bs->read_exactly(ws->bs, ws->rb, 1, get_header, ws);
	}
}

static void handle_pong_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	if (ws->on_control != NULL) {
		ws->on_control(ws, CIO_WEBSOCKET_PONG_FRAME, data, length);
	}

	ws->bs->read_exactly(ws->bs, ws->rb, 1, get_header, ws);
}

static void handle_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	if (ws->ws_flags.is_server == 1) {
		cio_websocket_mask(data, length, ws->received_mask);
	}

	switch (ws->ws_flags.opcode) {
	case CIO_WEBSOCKET_BINARY_FRAME:
		handle_binary_frame(ws, data, length, ws->ws_flags.fin == 1);
		break;

	case CIO_WEBSOCKET_TEXT_FRAME:
		handle_text_frame(ws, data, length, ws->ws_flags.fin == 1);
		break;

	case CIO_WEBSOCKET_PING_FRAME:
		handle_ping_frame(ws, data, length);
		break;

	case CIO_WEBSOCKET_PONG_FRAME:
		handle_pong_frame(ws, data, length);
		break;

	case CIO_WEBSOCKET_CLOSE_FRAME:
		handle_close_frame(ws, data, length);
		break;

	default:
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "reserved opcode used");
		break;
	}
}

static inline bool handled_read_error(struct cio_websocket *ws, enum cio_error err)
{
	if (unlikely(err != CIO_SUCCESS)) {
		if (err == CIO_EOF) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "connection closed by other peer");
		} else {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while reading websocket packet");
		}

		return true;
	}

	return false;
}

static void get_payload(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	(void)bs;

	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	size_t len = cio_read_buffer_get_transferred_bytes(buffer);

	handle_frame(ws, ptr, len);
}

static void get_mask(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);

	memcpy(ws->received_mask, ptr, sizeof(ws->received_mask));
	if (likely(ws->read_frame_length > 0)) {
		err = bs->read_exactly(bs, buffer, ws->read_frame_length, get_payload, ws);
		if (unlikely(err != CIO_SUCCESS)) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket payload");
		}
	} else {
		buffer->bytes_transferred = 0;
		handle_frame(ws, NULL, 0);
	}
}

static void get_mask_or_payload(struct cio_websocket *ws, struct cio_buffered_stream *bs, struct cio_read_buffer *buffer)
{
	enum cio_error err;
	if (ws->ws_flags.is_server == 1) {
		err = bs->read_exactly(bs, buffer, sizeof(ws->received_mask), get_mask, ws);
		if (unlikely(err != CIO_SUCCESS)) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket mask");
		}

		return;
	} else {
		if (likely(ws->read_frame_length > 0)) {
			err = bs->read_exactly(bs, buffer, ws->read_frame_length, get_payload, ws);
			if (unlikely(err != CIO_SUCCESS)) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket payload");
			}

			return;
		} else {
			buffer->bytes_transferred = 0;
			handle_frame(ws, NULL, 0);
		}
	}
}

static void get_length16(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);

	uint16_t field;
	memcpy(&field, ptr, sizeof(field));
	field = cio_be16toh(field);
	ws->read_frame_length = (uint64_t)field;
	get_mask_or_payload(ws, bs, buffer);
}

static void get_length64(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);

	uint64_t field;
	memcpy(&field, ptr, sizeof(field));
	field = cio_be64toh(field);
	ws->read_frame_length = field;
	get_mask_or_payload(ws, bs, buffer);
}

static inline bool is_control_frame(unsigned int opcode)
{
	return opcode >= CIO_WEBSOCKET_CLOSE_FRAME;
}

static void get_first_length(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	uint8_t field = *ptr;

	if (((field & WS_MASK_SET) == 0) && (ws->ws_flags.is_server == 1)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "received unmasked frame on server websocket");
		return;
	}

	field = field & ~WS_MASK_SET;
	if (field <= CIO_WEBSOCKET_SMALL_FRAME_SIZE) {
		ws->read_frame_length = (uint64_t)field;
		get_mask_or_payload(ws, bs, buffer);
	} else {
		if (unlikely(is_control_frame(ws->ws_flags.opcode))) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "payload of control frame too long");
			return;
		}

		if (field == CIO_WEBSOCKET_SMALL_FRAME_SIZE + 1) {
			err = bs->read_exactly(bs, buffer, 2, get_length16, ws);
		} else {
			err = bs->read_exactly(bs, buffer, 8, get_length64, ws);
		}
	}

	if (unlikely(err != CIO_SUCCESS)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading extended websocket frame length");
	}
}

static void get_header(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	uint8_t field = *ptr;

	if ((field & WS_HEADER_FIN) == WS_HEADER_FIN) {
		ws->ws_flags.fin = 1;
	} else {
		ws->ws_flags.fin = 0;
	}

	static const uint8_t RSV_MASK = 0x70;
	uint8_t rsv_field = field & RSV_MASK;
	if (unlikely(rsv_field != 0)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "reserved bit set in frame");
		return;
	}

	static const uint8_t OPCODE_MASK = 0x0f;
	field = field & OPCODE_MASK;

	if (unlikely((ws->ws_flags.fin == 0) && (field >= CIO_WEBSOCKET_CLOSE_FRAME))) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got fragmented control frame");
		return;
	}

	if (ws->ws_flags.fin == 1) {
		if (field != CIO_WEBSOCKET_CONTINUATION_FRAME) {
			if (unlikely(ws->ws_flags.frag_opcode && !is_control_frame(field))) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got non-continuation frame within fragmented stream");
				return;
			}

			ws->ws_flags.opcode = field;
		} else {
			if (unlikely(!ws->ws_flags.frag_opcode)) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got continuation frame without correct start frame");
				return;
			}

			ws->ws_flags.opcode = ws->ws_flags.frag_opcode;
			ws->ws_flags.frag_opcode = 0;
		}
	} else {
		if (field != CIO_WEBSOCKET_CONTINUATION_FRAME) {
			if (unlikely(ws->ws_flags.frag_opcode)) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got non-continuation frame within fragmented stream");
				return;
			}

			ws->ws_flags.frag_opcode = field;
			ws->ws_flags.opcode = field;
		} else {
			if (unlikely(!ws->ws_flags.frag_opcode)) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got continuation frame without correct start frame");
				return;
			}

			ws->ws_flags.opcode = ws->ws_flags.frag_opcode;
		}
	}

	err = bs->read_exactly(bs, buffer, 1, get_first_length, ws);
	if (unlikely(err != CIO_SUCCESS)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket frame length");
	}
}

static enum cio_error read_message(struct cio_websocket *ws, cio_websocket_read_handler handler, void *handler_context)
{
	if (unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	ws->read_handler = handler;
	ws->read_handler_context = handler_context;
	enum cio_error err = ws->bs->read_exactly(ws->bs, ws->rb, 1, get_header, ws);
	if (unlikely(err != CIO_SUCCESS)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket header");
	}

	return CIO_SUCCESS;
}

static enum cio_error write_message(struct cio_websocket *ws, struct cio_write_buffer *payload, bool last_frame, bool is_binary, cio_websocket_write_handler handler, void *handler_context)
{
	if (unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (unlikely(ws->write_message_job.wbh != NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	enum cio_websocket_frame_type kind;

	if (ws->ws_flags.fragmented_write == 1) {
		if (is_binary) {
			kind = CIO_WEBSOCKET_BINARY_FRAME;
		} else {
			kind = CIO_WEBSOCKET_TEXT_FRAME;
		}

		if (!last_frame) {
			ws->ws_flags.fragmented_write = 0;
		}
	} else {
		if (last_frame) {
			ws->ws_flags.fragmented_write = 1;
		}

		kind = CIO_WEBSOCKET_CONTINUATION_FRAME;
	}

	ws->write_message_job.wbh = payload;
	ws->write_message_job.handler = handler;
	ws->write_message_job.handler_context = handler_context;
	ws->write_message_job.frame_type = kind;
	ws->write_message_job.last_frame = last_frame;
	ws->write_message_job.stream_handler = message_written;

	return send_frame(ws, &ws->write_message_job);
}

static enum cio_error write_ping_message(struct cio_websocket *ws, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context)
{
	if (unlikely(payload_size_in_limit(payload, CIO_WEBSOCKET_SMALL_FRAME_SIZE) == 0)) {
		handler(ws, handler_context, CIO_MESSAGE_TOO_LONG);
		return CIO_INVALID_ARGUMENT;
	}

	if (unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (unlikely(ws->write_ping_job.wbh != NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	ws->write_ping_job.wbh = payload;
	ws->write_ping_job.handler = handler;
	ws->write_ping_job.handler_context = handler_context;
	ws->write_ping_job.frame_type = CIO_WEBSOCKET_PING_FRAME;
	ws->write_ping_job.last_frame = true;
	ws->write_ping_job.stream_handler = message_written;

	return send_frame(ws, &ws->write_ping_job);
}

static enum cio_error write_pong_message(struct cio_websocket *ws, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context)
{
	if (unlikely(payload_size_in_limit(payload, CIO_WEBSOCKET_SMALL_FRAME_SIZE) == 0)) {
		handler(ws, handler_context, CIO_MESSAGE_TOO_LONG);
		return CIO_INVALID_ARGUMENT;
	}

	if (unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (unlikely(ws->write_pong_job.wbh != NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	ws->write_pong_job.wbh = payload;
	ws->write_pong_job.handler = handler;
	ws->write_pong_job.handler_context = handler_context;
	ws->write_pong_job.frame_type = CIO_WEBSOCKET_PONG_FRAME;
	ws->write_pong_job.last_frame = true;
	ws->write_pong_job.stream_handler = message_written;

	return send_frame(ws, &ws->write_pong_job);
}

static enum cio_error write_close_message(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const char *reason, cio_websocket_write_handler handler, void *handler_context)
{
	if (unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (unlikely(ws->write_close_job.wbh != NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	prepare_close_job(ws, status_code, (const uint8_t *)reason, strlen(reason), handler, handler_context, close_frame_written);

	enum cio_error err = cio_timer_init(&ws->close_timer, ws->loop, NULL);
	if (unlikely(err != CIO_SUCCESS)) {
		goto err;
	}

	err = ws->close_timer.expires_from_now(&ws->close_timer, close_timeout_ns, close_timeout_handler, ws);
	if (unlikely(err != CIO_SUCCESS)) {
		goto err;
	}

	ws->ws_flags.self_initiated_close = 1;
	return send_frame(ws, &ws->write_close_job);
err:
	close(ws);
	return CIO_SUCCESS;
}

enum cio_error cio_websocket_init(struct cio_websocket *ws, bool is_server, cio_websocket_on_connect on_connect, cio_websocket_close_hook close_hook)
{
	if (unlikely(on_connect == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	ws->on_connect = on_connect;
	ws->read_message = read_message;
	ws->on_control = NULL;
	ws->read_handler = NULL;

	ws->on_error = NULL;
	ws->close = write_close_message;
	ws->write_message = write_message;
	ws->write_ping = write_ping_message;
	ws->write_pong = write_pong_message;
	ws->close_hook = close_hook;
	ws->ws_flags.is_server = is_server ? 1 : 0;
	ws->ws_flags.frag_opcode = 0;
	ws->ws_flags.self_initiated_close = 0;
	ws->ws_flags.fragmented_write = 1;

	ws->write_message_job.wbh = NULL;
	ws->write_ping_job.wbh = NULL;
	ws->write_pong_job.wbh = NULL;
	ws->write_close_job.wbh = NULL;

	ws->first_write_job = NULL;

	cio_utf8_init(&ws->utf8_state);

	return CIO_SUCCESS;
}
