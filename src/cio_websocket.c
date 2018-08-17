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

static const uint64_t close_timeout_ns = UINT64_C(10) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

static void get_header(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer);
static void handle_error(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const char *reason);

static void close(struct cio_websocket *ws)
{
	if (cio_likely(ws->ws_private.read_handler != NULL)) {
		ws->ws_private.read_handler(ws, ws->ws_private.read_handler_context, CIO_EOF, NULL, 0, false, false);
	}

	if (ws->ws_private.close_hook) {
		ws->ws_private.close_hook(ws);
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

static inline void swap(uint8_t mask[4], uint_fast8_t first, uint_fast8_t last)
{
	uint8_t tmp = mask[first];
	mask[first] = mask[last];
	mask[last] = tmp;
}

static inline void rotate(uint8_t mask[4], uint_fast8_t middle)
{
	uint_fast8_t first = 0;
	uint_fast8_t last = 4;
	uint_fast8_t next = middle;

	while (first != next) {
		swap(mask, first, next);
		first++;
		next++;
		if (next == last) {
			next = middle;
		} else if (first == middle) {
			middle = next;
		}
	}
}

static void mask_write_buffer(struct cio_write_buffer *wb, uint8_t mask[4])
{
	size_t num_buffers = wb->data.q_len;
	for (size_t i = 0; i < num_buffers; i++) {
		wb = wb->next;
		cio_websocket_mask(wb->data.element.data, wb->data.element.length, mask);
		size_t middle = wb->data.element.length % 4;
		rotate(mask, (uint_fast8_t)middle);
	}
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

	if (ws->ws_private.ws_flags.is_server == 0) {
		first_len |= WS_MASK_SET;
		uint8_t mask[4];
		cio_random_get_bytes(mask, sizeof(mask));
		memcpy(&job->send_header[header_index], &mask, sizeof(mask));
		header_index += sizeof(mask);
		mask_write_buffer(job->wbh, mask);
	}

	job->send_header[1] = first_len;

	cio_write_buffer_element_init(&job->websocket_header, job->send_header, header_index);
	add_websocket_header(job);
	return ws->ws_private.bs->write(ws->ws_private.bs, job->wbh, job->stream_handler, ws);
}

static enum cio_error enqueue_job(struct cio_websocket *ws, struct cio_websocket_write_job *job)
{
	if (ws->ws_private.first_write_job == NULL) {
		ws->ws_private.first_write_job = job;
		ws->ws_private.last_write_job = job;
		enum cio_error err = send_frame(ws, job);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "Could not send frame");
			return err;
		}
	} else {
		ws->ws_private.last_write_job->next = job;
		ws->ws_private.last_write_job = job;
	}

	return CIO_SUCCESS;
}

static struct cio_websocket_write_job *dequeue_job(struct cio_websocket *ws)
{
	struct cio_websocket_write_job *job = ws->ws_private.first_write_job;
	if (job == NULL) {
		return NULL;
	}

	if (ws->ws_private.first_write_job == ws->ws_private.last_write_job) {
		ws->ws_private.first_write_job = NULL;
	} else {
		ws->ws_private.first_write_job = job->next;
	}

	return job;
}

static void abort_write_jobs(struct cio_websocket *ws)
{
	struct cio_websocket_write_job *job = dequeue_job(ws);

	while (job != NULL) {
		job->wbh = NULL;
		if (job->handler) {
			job->handler(ws, job->handler_context, CIO_OPERATION_ABORTED);
		}

		job = dequeue_job(ws);
	}
}

static void prepare_close_job(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const uint8_t *reason, size_t reason_length, cio_websocket_write_handler handler, void *handler_context, cio_buffered_stream_write_handler stream_handler)
{
	uint16_t sc = (uint16_t)status_code;
	size_t close_buffer_length = sizeof(sc);
	sc = cio_htobe16(sc);
	memcpy(ws->ws_private.close_buffer.buffer, &sc, sizeof(sc));
	if (reason != NULL) {
		size_t copy_len = MIN(reason_length, sizeof(ws->ws_private.close_buffer.buffer) - sizeof(sc));
		memcpy(ws->ws_private.close_buffer.buffer + sizeof(sc), reason, copy_len);
		close_buffer_length += copy_len;
	}

	cio_write_buffer_head_init(&ws->ws_private.close_buffer.wb_head);
	cio_write_buffer_element_init(&ws->ws_private.close_buffer.wb, ws->ws_private.close_buffer.buffer, close_buffer_length);
	cio_write_buffer_queue_tail(&ws->ws_private.close_buffer.wb_head, &ws->ws_private.close_buffer.wb);
	ws->ws_private.write_close_job.wbh = &ws->ws_private.close_buffer.wb_head;
	ws->ws_private.write_close_job.handler = handler;
	ws->ws_private.write_close_job.handler_context = handler_context;
	ws->ws_private.write_close_job.frame_type = CIO_WEBSOCKET_CLOSE_FRAME;
	ws->ws_private.write_close_job.last_frame = true;
	ws->ws_private.write_close_job.stream_handler = stream_handler;
}

static void prepare_close_job_string(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const char *reason, cio_websocket_write_handler handler, void *handler_context, cio_buffered_stream_write_handler stream_handler)
{
	size_t length;

	if (reason == NULL) {
		length = 0;
	} else {
		length = strlen(reason);
	}

	prepare_close_job(ws, status_code, (const uint8_t *)reason, length, handler, handler_context, stream_handler);
}

static void close_frame_written(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err);

static void handle_error(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const char *reason)
{
	if (ws->ws_private.ws_flags.closed_by_error == 0) {
		ws->ws_private.ws_flags.closed_by_error = 1;
		if (ws->on_error != NULL) {
			ws->on_error(ws, status_code, reason);
		}

		abort_write_jobs(ws);
		prepare_close_job_string(ws, status_code, reason, NULL, NULL, close_frame_written);
		enqueue_job(ws, &ws->ws_private.write_close_job);

		close(ws);
	}
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

	if (cio_likely(job->handler != NULL)) {
		job->handler(ws, job->handler_context, err);
	}

	if (ws->ws_private.first_write_job != NULL) {
		 err = send_frame(ws, ws->ws_private.first_write_job);
		 if (cio_unlikely(err != CIO_SUCCESS)) {
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

	if (job->handler) {
		job->handler(ws, job->handler_context, err);
	}

	abort_write_jobs(ws);
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

static void close_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;
	if (err != CIO_OPERATION_ABORTED) {
		struct cio_websocket *ws = (struct cio_websocket *)handler_context;
		ws->ws_private.close_timer.close(&ws->ws_private.close_timer);
		close(ws);
	}
}

static int payload_size_in_limit(const struct cio_write_buffer *payload, size_t limit)
{
	if (cio_likely(payload != NULL)) {
		size_t payload_length = 0;

		const struct cio_write_buffer *element = payload->next;
		while (element != payload) {
			payload_length += element->data.element.length;
			element = element->next;
		}

		if (cio_unlikely(payload_length > limit)) {
			return 0;
		}
	}

	return 1;
}

static void handle_binary_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length, bool last_frame)
{
	ws->ws_private.read_handler(ws, ws->ws_private.read_handler_context, CIO_SUCCESS, data, length, last_frame, true);
}

static void handle_text_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length, bool last_frame)
{
	enum cio_utf8_status status = cio_check_utf8(&ws->ws_private.utf8_state, data, length);

	if (cio_unlikely((status == CIO_UTF8_REJECT) || (last_frame && (status != CIO_UTF8_ACCEPT)))) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA, "payload not valid utf8");
		return;
	}

	ws->ws_private.read_handler(ws, ws->ws_private.read_handler_context, CIO_SUCCESS, data, length, last_frame, false);
	if (last_frame) {
		cio_utf8_init(&ws->ws_private.utf8_state);
	}
}

static void handle_close_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	uint64_t len = length;

	if (cio_unlikely(length == 1)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "close payload of length 1");
		return;
	}

	uint16_t status_code;
	if (length >= 2) {
		memcpy(&status_code, data, sizeof(status_code));
		status_code = cio_be16toh(status_code);
		if (cio_unlikely(is_invalid_status_code(status_code))) {
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
		if (cio_unlikely(cio_check_utf8(&state, data + 2, length - 2) != CIO_UTF8_ACCEPT)) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA, "reason in close frame not utf8 valid");
			return;
		}
	}

	if (ws->on_control != NULL) {
		ws->on_control(ws, CIO_WEBSOCKET_CLOSE_FRAME, data, len);
	}

	if (ws->ws_private.ws_flags.self_initiated_close == 1) {
		ws->ws_private.close_timer.cancel(&ws->ws_private.close_timer);
		ws->ws_private.close_timer.close(&ws->ws_private.close_timer);
		close(ws);
	} else {
		const uint8_t *reason;
		if (length > 0) {
			reason = data + sizeof(status_code);
		} else {
			reason = NULL;
		}

		prepare_close_job(ws, status_code, reason, length, NULL, NULL, response_close_frame_written);
		enqueue_job(ws, &ws->ws_private.write_close_job);
	}
}

static void handle_ping_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	cio_write_buffer_head_init(&ws->ws_private.ping_buffer.wb_head);
	if (length > 0) {
		memcpy(ws->ws_private.ping_buffer.buffer, data, length);
		cio_write_buffer_element_init(&ws->ws_private.ping_buffer.wb, ws->ws_private.ping_buffer.buffer, length);
		cio_write_buffer_queue_tail(&ws->ws_private.ping_buffer.wb_head, &ws->ws_private.ping_buffer.wb);
	}

	if (ws->on_control != NULL) {
		ws->on_control(ws, CIO_WEBSOCKET_PING_FRAME, data, length);
	}

	ws->ws_private.write_pong_job.wbh = &ws->ws_private.ping_buffer.wb_head;
	ws->ws_private.write_pong_job.handler = NULL;
	ws->ws_private.write_pong_job.handler_context = NULL;
	ws->ws_private.write_pong_job.frame_type = CIO_WEBSOCKET_PONG_FRAME;
	ws->ws_private.write_pong_job.last_frame = true;
	ws->ws_private.write_pong_job.stream_handler = message_written;

	enum cio_error err = enqueue_job(ws, &ws->ws_private.write_pong_job);
	if (cio_likely(err == CIO_SUCCESS)) {
		ws->ws_private.bs->read_exactly(ws->ws_private.bs, ws->ws_private.rb, 1, get_header, ws);
	}
}

static void handle_pong_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	if (ws->on_control != NULL) {
		ws->on_control(ws, CIO_WEBSOCKET_PONG_FRAME, data, length);
	}

	ws->ws_private.bs->read_exactly(ws->ws_private.bs, ws->ws_private.rb, 1, get_header, ws);
}

static void handle_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	if (ws->ws_private.ws_flags.is_server == 1) {
		cio_websocket_mask(data, length, ws->ws_private.received_mask);
	}

	switch (ws->ws_private.ws_flags.opcode) {
	case CIO_WEBSOCKET_BINARY_FRAME:
		handle_binary_frame(ws, data, length, ws->ws_private.ws_flags.fin == 1);
		break;

	case CIO_WEBSOCKET_TEXT_FRAME:
		handle_text_frame(ws, data, length, ws->ws_private.ws_flags.fin == 1);
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
	if (cio_unlikely(err != CIO_SUCCESS)) {
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
	if (cio_unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	size_t len = cio_read_buffer_get_transferred_bytes(buffer);

	handle_frame(ws, ptr, len);
}

static void get_mask(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (cio_unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);

	memcpy(ws->ws_private.received_mask, ptr, sizeof(ws->ws_private.received_mask));
	if (cio_likely(ws->ws_private.read_frame_length > 0)) {
		err = bs->read_exactly(bs, buffer, ws->ws_private.read_frame_length, get_payload, ws);
		if (cio_unlikely(err != CIO_SUCCESS)) {
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
	if (ws->ws_private.ws_flags.is_server == 1) {
		err = bs->read_exactly(bs, buffer, sizeof(ws->ws_private.received_mask), get_mask, ws);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket mask");
		}
	} else {
		if (cio_likely(ws->ws_private.read_frame_length > 0)) {
			err = bs->read_exactly(bs, buffer, ws->ws_private.read_frame_length, get_payload, ws);
			if (cio_unlikely(err != CIO_SUCCESS)) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket payload");
			}
		} else {
			buffer->bytes_transferred = 0;
			handle_frame(ws, NULL, 0);
		}
	}
}

static void get_length16(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (cio_unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);

	uint16_t field;
	memcpy(&field, ptr, sizeof(field));
	field = cio_be16toh(field);
	ws->ws_private.read_frame_length = (uint64_t)field;
	get_mask_or_payload(ws, bs, buffer);
}

static void get_length64(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (cio_unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);

	uint64_t field;
	memcpy(&field, ptr, sizeof(field));
	field = cio_be64toh(field);
	ws->ws_private.read_frame_length = field;
	get_mask_or_payload(ws, bs, buffer);
}

static inline bool is_control_frame(unsigned int opcode)
{
	return opcode >= CIO_WEBSOCKET_CLOSE_FRAME;
}

static void get_first_length(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (cio_unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	uint8_t field = *ptr;

	if (((field & WS_MASK_SET) == 0) && (ws->ws_private.ws_flags.is_server == 1)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "received unmasked frame on server websocket");
		return;
	}

	field = field & (uint8_t)(~WS_MASK_SET);
	if (field <= CIO_WEBSOCKET_SMALL_FRAME_SIZE) {
		ws->ws_private.read_frame_length = (uint64_t)field;
		get_mask_or_payload(ws, bs, buffer);
	} else {
		if (cio_unlikely(is_control_frame(ws->ws_private.ws_flags.opcode))) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "payload of control frame too long");
			return;
		}

		if (field == CIO_WEBSOCKET_SMALL_FRAME_SIZE + 1) {
			err = bs->read_exactly(bs, buffer, 2, get_length16, ws);
		} else {
			err = bs->read_exactly(bs, buffer, 8, get_length64, ws);
		}
	}

	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading extended websocket frame length");
	}
}

static void get_header(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (cio_unlikely(handled_read_error(ws, err))) {
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	uint8_t field = *ptr;

	ws->ws_private.ws_flags.fin = (field & WS_HEADER_FIN) == WS_HEADER_FIN;

	static const uint8_t RSV_MASK = 0x70;
	uint8_t rsv_field = field & RSV_MASK;
	if (cio_unlikely(rsv_field != 0)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "reserved bit set in frame");
		return;
	}

	static const uint8_t OPCODE_MASK = 0x0f;
	field = field & OPCODE_MASK;

	if (cio_unlikely((ws->ws_private.ws_flags.fin == 0) && (field >= CIO_WEBSOCKET_CLOSE_FRAME))) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got fragmented control frame");
		return;
	}

	if (ws->ws_private.ws_flags.fin == 1) {
		if (field != CIO_WEBSOCKET_CONTINUATION_FRAME) {
			if (cio_unlikely(ws->ws_private.ws_flags.frag_opcode && !is_control_frame(field))) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got non-continuation frame within fragmented stream");
				return;
			}

			ws->ws_private.ws_flags.opcode = field;
		} else {
			if (cio_unlikely(!ws->ws_private.ws_flags.frag_opcode)) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got continuation frame without correct start frame");
				return;
			}

			ws->ws_private.ws_flags.opcode = ws->ws_private.ws_flags.frag_opcode;
			ws->ws_private.ws_flags.frag_opcode = 0;
		}
	} else {
		if (field != CIO_WEBSOCKET_CONTINUATION_FRAME) {
			if (cio_unlikely(ws->ws_private.ws_flags.frag_opcode)) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got non-continuation frame within fragmented stream");
				return;
			}

			ws->ws_private.ws_flags.frag_opcode = field;
			ws->ws_private.ws_flags.opcode = field;
		} else {
			if (cio_unlikely(!ws->ws_private.ws_flags.frag_opcode)) {
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "got continuation frame without correct start frame");
				return;
			}

			ws->ws_private.ws_flags.opcode = ws->ws_private.ws_flags.frag_opcode;
		}
	}

	err = bs->read_exactly(bs, buffer, 1, get_first_length, ws);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket frame length");
	}
}

static enum cio_error read_message(struct cio_websocket *ws, cio_websocket_read_handler handler, void *handler_context)
{
	if (cio_unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	ws->ws_private.read_handler = handler;
	ws->ws_private.read_handler_context = handler_context;
	enum cio_error err = ws->ws_private.bs->read_exactly(ws->ws_private.bs, ws->ws_private.rb, 1, get_header, ws);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket header");
	}

	return CIO_SUCCESS;
}

static enum cio_error write_message(struct cio_websocket *ws, struct cio_write_buffer *payload, bool last_frame, bool is_binary, cio_websocket_write_handler handler, void *handler_context)
{
	if (cio_unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely(ws->ws_private.write_message_job.wbh != NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	enum cio_websocket_frame_type kind;

	if (ws->ws_private.ws_flags.fragmented_write == 1) {
		if (is_binary) {
			kind = CIO_WEBSOCKET_BINARY_FRAME;
		} else {
			kind = CIO_WEBSOCKET_TEXT_FRAME;
		}

		if (!last_frame) {
			ws->ws_private.ws_flags.fragmented_write = 0;
		}
	} else {
		if (last_frame) {
			ws->ws_private.ws_flags.fragmented_write = 1;
		}

		kind = CIO_WEBSOCKET_CONTINUATION_FRAME;
	}

	ws->ws_private.write_message_job.wbh = payload;
	ws->ws_private.write_message_job.handler = handler;
	ws->ws_private.write_message_job.handler_context = handler_context;
	ws->ws_private.write_message_job.frame_type = kind;
	ws->ws_private.write_message_job.last_frame = last_frame;
	ws->ws_private.write_message_job.stream_handler = message_written;

	enqueue_job(ws, &ws->ws_private.write_message_job);
	return CIO_SUCCESS;
}

static enum cio_error write_ping_or_pong_message(struct cio_websocket *ws, enum cio_websocket_frame_type frame_type, struct cio_websocket_write_job *job, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context)
{
	if (cio_unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely(payload_size_in_limit(payload, CIO_WEBSOCKET_SMALL_FRAME_SIZE) == 0)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely(job->wbh != NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	job->wbh = payload;
	job->handler = handler;
	job->handler_context = handler_context;
	job->frame_type = frame_type;
	job->last_frame = true;
	job->stream_handler = message_written;

	enqueue_job(ws, job);
	return CIO_SUCCESS;
}

static enum cio_error write_ping_message(struct cio_websocket *ws, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context)
{
	return write_ping_or_pong_message(ws, CIO_WEBSOCKET_PING_FRAME, &ws->ws_private.write_ping_job, payload, handler, handler_context);
}

static enum cio_error write_pong_message(struct cio_websocket *ws, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context)
{
	return write_ping_or_pong_message(ws, CIO_WEBSOCKET_PONG_FRAME, &ws->ws_private.write_pong_job, payload, handler, handler_context);
}

static enum cio_error write_close_message(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const char *reason, cio_websocket_write_handler handler, void *handler_context)
{
	if (cio_unlikely((ws == NULL)) || (handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely(ws->ws_private.write_close_job.wbh != NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	prepare_close_job_string(ws, status_code, reason, handler, handler_context, close_frame_written);

	enum cio_error err = cio_timer_init(&ws->ws_private.close_timer, ws->ws_private.loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	err = ws->ws_private.close_timer.expires_from_now(&ws->ws_private.close_timer, close_timeout_ns, close_timeout_handler, ws);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto timer_expires_failed;
	}

	ws->ws_private.ws_flags.self_initiated_close = 1;
	enqueue_job(ws, &ws->ws_private.write_close_job);

	return CIO_SUCCESS;

timer_expires_failed:
	ws->ws_private.close_timer.close(&ws->ws_private.close_timer);
	return err;
}

enum cio_error cio_websocket_init(struct cio_websocket *ws, bool is_server, cio_websocket_on_connect on_connect, cio_websocket_close_hook close_hook)
{
	if (cio_unlikely((ws == NULL) || (on_connect == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	ws->on_connect = on_connect;
	ws->read_message = read_message;
	ws->on_control = NULL;
	ws->ws_private.read_handler = NULL;

	ws->on_error = NULL;
	ws->close = write_close_message;
	ws->write_message = write_message;
	ws->write_ping = write_ping_message;
	ws->write_pong = write_pong_message;
	ws->ws_private.close_hook = close_hook;
	ws->ws_private.ws_flags.is_server = is_server ? 1 : 0;
	ws->ws_private.ws_flags.frag_opcode = 0;
	ws->ws_private.ws_flags.self_initiated_close = 0;
	ws->ws_private.ws_flags.fragmented_write = 1;
	ws->ws_private.ws_flags.closed_by_error = 0;

	ws->ws_private.write_message_job.wbh = NULL;
	ws->ws_private.write_ping_job.wbh = NULL;
	ws->ws_private.write_pong_job.wbh = NULL;
	ws->ws_private.write_close_job.wbh = NULL;

	ws->ws_private.first_write_job = NULL;

	cio_utf8_init(&ws->ws_private.utf8_state);

	return CIO_SUCCESS;
}
