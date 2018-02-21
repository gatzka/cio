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
#include "cio_util.h"
#include "cio_websocket.h"
#include "cio_websocket_masking.h"

#undef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static const uint8_t WS_MASK_SET = 0x80;
static const uint8_t WS_HEADER_FIN = 0x80;
static const unsigned int WS_MID_FRAME_SIZE = 65535;

static const uint64_t close_timeout_ns = UINT64_C(10) * UINT64_C(1000) * UINT64_C(1000);

enum close_handling {
	CLOSE_IMMEDIATE,
	CLOSE_WEBSOCKET_HANDSHAKE
};

enum cio_ws_frame_type {
	CIO_WEBSOCKET_CONTINUATION_FRAME = 0x0,
	CIO_WEBSOCKET_TEXT_FRAME = 0x1,
	CIO_WEBSOCKET_BINARY_FRAME = 0x2,
	CIO_WEBSOCKET_CLOSE_FRAME = 0x8,
	CIO_WEBSOCKET_PING_FRAME = 0x9,
	CIO_WEBSOCKET_PONG_FRAME = 0x0a,
};

static void close(struct cio_websocket *ws)
{
	if (ws->ws_flags.handle_frame_ctx == 0) {
		if (ws->ws_flags.self_initiated_close == 1) {
			ws->close_timer.close(&ws->close_timer);
		}

		if (ws->close_hook) {
			ws->close_hook(ws);
		}
	}
}

static void write_complete(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	ws->ws_flags.writing_frame = 0;
	ws->user_write_handler(bs, handler_context, buffer, err);
}

static void send_frame(struct cio_websocket *ws, struct cio_write_buffer *payload, enum cio_ws_frame_type frame_type, bool last_frame, cio_buffered_stream_write_handler written_cb)
{
	if (unlikely(ws->ws_flags.writing_frame == 1)) {
		/*
		 * There is a frame that is still not written completely.
		 * "Normal" library user code is not allowed to send a new frame if a previous
		 * frame was not written completly (i.e. the write callback function was not called yet).
		 * But if the library receives a control frame (a ping or a close frame), the library
		 * itself wants to write a response frame (pong or close). If there's still an unwritten
		 * frame hanging around, we close the TCP connection.
		 *
		 * With the expense of more memory, we could optimize that behavior slightly. We can copy
		 * the application data of the control frames into some buffer, set a flag that there is a control
		 * frame to write and write this frame after the first write completes.
		 */
		ws->ws_flags.to_be_closed = 1;
		return;
	}

	uint8_t first_len;
	size_t header_index = 2;

	size_t length = 0;
	cio_write_buffer_head_init(&ws->wbh);
	if (unlikely(payload != NULL)) {
		struct cio_write_buffer *element = payload;
		for (size_t i = 0; i < payload->data.q_len; i++) {
			element = element->next;
			length += element->data.element.length;
		}

		cio_write_buffer_splice(payload, &ws->wbh);
	}

	uint8_t first_byte;
	if (last_frame) {
		first_byte = (uint8_t)frame_type;
		first_byte |= WS_HEADER_FIN;
	} else {
		first_byte = CIO_WEBSOCKET_CONTINUATION_FRAME;
	}

	ws->send_header[0] = first_byte;

	if (length <= CIO_WEBSOCKET_SMALL_FRAME_SIZE) {
		first_len = (uint8_t)length;
	} else if (length <= WS_MID_FRAME_SIZE) {
		uint16_t be_len = cio_htobe16((uint16_t)length);
		memcpy(&ws->send_header[2], &be_len, sizeof(be_len));
		header_index += sizeof(be_len);
		first_len = CIO_WEBSOCKET_SMALL_FRAME_SIZE + 1;
	} else {
		uint64_t be_len = cio_htobe64((uint64_t)length);
		memcpy(&ws->send_header[2], &be_len, sizeof(be_len));
		header_index += sizeof(be_len);
		first_len = CIO_WEBSOCKET_SMALL_FRAME_SIZE + 2;
	}

	if (ws->ws_flags.is_server == 0) {
		first_len |= WS_MASK_SET;
		uint8_t mask[4];
		cio_random_get_bytes(mask, sizeof(mask));
		memcpy(&ws->send_header[header_index], &mask, sizeof(mask));
		header_index += sizeof(mask);
		// TODO: cio_websocket_mask(payload, length, mask );
	}

	ws->send_header[1] = first_len;

	cio_write_buffer_element_init(&ws->wb_send_header, ws->send_header, header_index);
	cio_write_buffer_queue_head(&ws->wbh, &ws->wb_send_header);
	ws->user_write_handler = written_cb;
	ws->ws_flags.writing_frame = 1;
	enum cio_error err = ws->bs->write(ws->bs, &ws->wbh, write_complete, ws);
	if (err != CIO_SUCCESS) {
		//TODO: check return value
		return;
	}
}

static bool is_status_code_invalid(uint16_t status_code)
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

static void close_frame_written(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	(void)bs;
	(void)buffer;
	(void)err;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (ws->ws_flags.self_initiated_close == 0) {
		close(ws);
	}
}

static void do_nothing(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	(void)bs;
	(void)handler_context;
	(void)buffer;
	(void)err;
}

static void close_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;
	if (err != CIO_OPERATION_ABORTED) {
		struct cio_websocket *ws = (struct cio_websocket *)handler_context;
		close(ws);
	}
}

static void send_close_frame_and_close(struct cio_websocket *ws, enum cio_websocket_status_code status_code, struct cio_write_buffer *reason)
{
	ws->close_status = cio_htobe16(status_code);

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_queue_tail(&wbh, &ws->wb_close_status);
	if (reason != NULL) {
		cio_write_buffer_splice(reason, &wbh);
	}

	send_frame(ws, &wbh, CIO_WEBSOCKET_CLOSE_FRAME, true, do_nothing);
	ws->ws_flags.to_be_closed = 1;
}

static void send_close_frame_wait_for_response(struct cio_websocket *ws, enum cio_websocket_status_code status_code, struct cio_write_buffer *reason)
{
	ws->close_status = cio_htobe16(status_code);

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_queue_tail(&wbh, &ws->wb_close_status);
	if (reason != NULL) {
		cio_write_buffer_splice(reason, &wbh);
	}

	enum cio_error err = cio_timer_init(&ws->close_timer, ws->loop, NULL);
	if (unlikely(err != CIO_SUCCESS)) {
		goto err;
	}

	err = ws->close_timer.expires_from_now(&ws->close_timer, close_timeout_ns, close_timeout_handler, ws);
	if (unlikely(err != CIO_SUCCESS)) {
		goto err;
	}

	ws->ws_flags.self_initiated_close = 1;
	send_frame(ws, &wbh, CIO_WEBSOCKET_CLOSE_FRAME, true, close_frame_written);
	return;

err:
	send_close_frame_and_close(ws, status_code, reason);
}

static void handle_binary_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length, bool last_frame)
{
	if (likely(ws->on_binaryframe != NULL)) {
		ws->on_binaryframe(ws, data, length, last_frame);
	} else {
		// TODO: handle_error(s, WS_CLOSE_UNSUPPORTED);
	}
}

static void handle_text_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length, bool last_frame)
{
	if (likely(ws->on_textframe != NULL)) {
		ws->on_textframe(ws, (char *)data, length, last_frame);
	} else {
		// TODO: handle_error(s, WS_CLOSE_UNSUPPORTED);
	}
}

static void handle_close_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	if (unlikely(length == 1)) {
		//handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
	}

	uint16_t status_code;
	if (length >= 2) {
		memcpy(&status_code, data, sizeof(status_code));
		status_code = cio_be16toh(status_code);
	} else {
		status_code = CIO_WEBSOCKET_CLOSE_NORMAL;
	}

	if (unlikely(is_status_code_invalid(status_code))) {
		//handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
	}

	const char *reason;
	if (length > 2) {
		// TODO: struct cjet_utf8_checker c;
		// TODO: cjet_init_checker(&c);
		// TODO: if (!cjet_is_byte_sequence_valid(&c, frame + 2, length - 2, true)) {
		// TODO: 	handle_error(s, WS_CLOSE_UNSUPPORTED_DATA);
		// TODO: }

		length -= sizeof(status_code);
		reason = (const char *)&data[sizeof(status_code)];
	} else {
		length = 0;
		reason = NULL;
	}

	if (ws->ws_flags.self_initiated_close == 1) {
		ws->close_timer.cancel(&ws->close_timer);
		ws->ws_flags.to_be_closed = 1;
	} else {
		if (ws->on_close != NULL) {
			ws->on_close(ws, (enum cio_websocket_status_code)status_code, reason, length);
		}

		if (length > 0) {
			memcpy(ws->send_control_frame_buffer, &data[sizeof(status_code)], length);
			struct cio_write_buffer wbh;
			cio_write_buffer_head_init(&wbh);
			cio_write_buffer_element_init(&ws->wb_control_data, ws->send_control_frame_buffer, length);
			cio_write_buffer_queue_tail(&wbh, &ws->wb_control_data);
			send_close_frame_and_close(ws, (enum cio_websocket_status_code)status_code, &wbh);
		} else {
			send_close_frame_and_close(ws, (enum cio_websocket_status_code)status_code, NULL);
		}
	}
}

static int payload_size_in_limit(const struct cio_write_buffer *payload, size_t limit)
{
	if (payload != NULL) {
		size_t payload_length = 0;

		const struct cio_write_buffer *element = payload;
		for (size_t i = 0; i < payload->data.q_len; i++) {
			element = element->next;
			payload_length += element->data.element.length;
		}

		if (unlikely(payload_length > limit)) {
			return 0;
		}
	}

	return 1;
}

static void self_close_frame(struct cio_websocket *ws, enum cio_websocket_status_code status_code, struct cio_write_buffer *reason)
{
	send_close_frame_wait_for_response(ws, status_code, reason);
}

static void handle_error(struct cio_websocket *ws, enum cio_websocket_status_code status_code, const char *reason)
{
	if (ws->on_error != NULL) {
		ws->on_error(ws, status_code, reason);
	}

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	strncpy((char *)ws->send_control_frame_buffer, reason, sizeof(ws->send_control_frame_buffer));
	cio_write_buffer_element_init(&ws->wb_control_data, ws->send_control_frame_buffer, MIN(strlen(reason) - sizeof(status_code), CIO_WEBSOCKET_SMALL_FRAME_SIZE - sizeof(status_code)));
	cio_write_buffer_queue_tail(&wbh, &ws->wb_control_data);

	send_close_frame_and_close(ws, status_code, &wbh);
	close(ws);
}

static void get_header(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer);

static void handle_ping_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	struct cio_write_buffer wbh;
	struct cio_write_buffer *payload;
	if (length > 0) {
		memcpy(ws->send_control_frame_buffer, data, length);
		cio_write_buffer_head_init(&wbh);
		cio_write_buffer_element_init(&ws->wb_control_data, ws->send_control_frame_buffer, length);
		cio_write_buffer_queue_tail(&wbh, &ws->wb_control_data);
		payload = &wbh;
	} else {
		payload = NULL;
	}

	send_frame(ws, payload, CIO_WEBSOCKET_PONG_FRAME, true, do_nothing);
	if (unlikely(ws->ws_flags.to_be_closed == 1)) {
		return;
	}

	if (ws->on_ping != NULL) {
		ws->on_ping(ws, data, length);
	}
}

static void handle_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	ws->ws_flags.handle_frame_ctx = 1;

	if (unlikely(ws->ws_flags.rsv != 0)) {
	//AB tested
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR , "reserved bit set in frame");
		goto out;
	}

	if (unlikely((ws->ws_flags.is_server == 1) && (ws->ws_flags.shall_mask == 0))) {
		// TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		goto out;
	}


	if (unlikely((ws->ws_flags.fin == 0) && (ws->ws_flags.opcode >= CIO_WEBSOCKET_CLOSE_FRAME))) {
		// TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		goto out;
	}

	if (ws->ws_flags.fin == 0) {
		if (ws->ws_flags.opcode != CIO_WEBSOCKET_CONTINUATION_FRAME) {
			if (unlikely(ws->ws_flags.is_fragmented)) {
				// TODO: log_err("Overwriting Opcode of unfinished fragmentation!");
				// TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
				goto out;
			}

			ws->ws_flags.is_fragmented = 1;
			ws->ws_flags.frag_opcode = ws->ws_flags.opcode;
			ws->ws_flags.opcode = CIO_WEBSOCKET_CONTINUATION_FRAME;
		} else {
			if (unlikely(!(ws->ws_flags.is_fragmented))) {
				//TODO: log_err("No start frame was send!");
				//TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
				handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR , "opcode for continuation frame not correct");
				goto out;
			}
		}
	}

	if (unlikely(ws->ws_flags.is_fragmented && ((ws->ws_flags.opcode < CIO_WEBSOCKET_CLOSE_FRAME) && (ws->ws_flags.opcode > CIO_WEBSOCKET_CONTINUATION_FRAME)))) {
	//AB tested
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR , "opcode for continuation frame not correct");
		goto out;
	}

	if (ws->ws_flags.shall_mask != 0) {
		cio_websocket_mask(data, length, ws->mask);
	}

	bool last_frame;
	if (ws->ws_flags.fin == 1) {
		last_frame = true;
	} else {
		last_frame = false;
	}

	unsigned int opcode;
	if (ws->ws_flags.opcode == CIO_WEBSOCKET_CONTINUATION_FRAME) {
		opcode = ws->ws_flags.frag_opcode;
	} else {
		opcode = ws->ws_flags.opcode;
	}

	if (unlikely(((opcode >= CIO_WEBSOCKET_CLOSE_FRAME) && (opcode <= CIO_WEBSOCKET_PONG_FRAME)) && (length > CIO_WEBSOCKET_SMALL_FRAME_SIZE))) {
	//AB tested
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, "payload of control frame too long");
		goto out;
	}

	switch (opcode) {
	case CIO_WEBSOCKET_BINARY_FRAME:
		handle_binary_frame(ws, data, length, last_frame);
		break;

	case CIO_WEBSOCKET_TEXT_FRAME:
		handle_text_frame(ws, data, length, last_frame);
		break;

	case CIO_WEBSOCKET_PING_FRAME:
		handle_ping_frame(ws, data, length);
		break;

	case CIO_WEBSOCKET_PONG_FRAME:
		if (ws->on_pong != NULL) {
			ws->on_pong(ws, data, length);
		}

		break;

	case CIO_WEBSOCKET_CLOSE_FRAME:
		handle_close_frame(ws, data, length);
		break;

	default:
	//AB tested
		handle_error(ws, CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR , "reserved opcode used");
		break;
	}

out:
	ws->ws_flags.handle_frame_ctx = 0;
	if (ws->ws_flags.to_be_closed == 1) {
		close(ws);
	} else {
		ws->bs->read_exactly(ws->bs, ws->rb, 1, get_header, ws);
	}
}

static void get_payload(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	(void)bs;

	size_t len = cio_read_buffer_get_transferred_bytes(buffer);
	if (unlikely((err != CIO_SUCCESS) || (len == 0))) {
		// TODO: fill out
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;

	handle_frame(ws, ptr, len);
}

static void get_mask(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	size_t len = cio_read_buffer_get_transferred_bytes(buffer);
	if (unlikely((err != CIO_SUCCESS) || (len == 0))) {
		// TODO: fill out
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;

	memcpy(ws->mask, ptr, sizeof(ws->mask));
	if (likely(ws->read_frame_length > 0)) {
		err = bs->read_exactly(bs, buffer, ws->read_frame_length, get_payload, ws);
		if (unlikely(err != CIO_SUCCESS)) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_TOO_LARGE, "payload length too large to handle");
		}
	} else {
		buffer->bytes_transferred = 0;
		handle_frame(ws, NULL, 0);
	}
}

static void get_mask_or_payload(struct cio_websocket *ws, struct cio_buffered_stream *bs, struct cio_read_buffer *buffer)
{
	if (ws->ws_flags.shall_mask == 1) {
		bs->read_exactly(bs, buffer, sizeof(ws->mask), get_mask, ws);
	} else {
		if (likely(ws->read_frame_length > 0)) {
			bs->read_exactly(bs, buffer, ws->read_frame_length, get_payload, ws);
		} else {
			handle_frame(ws, NULL, 0);
		}
	}
}

static void get_length16(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	size_t len = cio_read_buffer_get_transferred_bytes(buffer);
	if (unlikely((err != CIO_SUCCESS) || (len == 0))) {
		// TODO: fill out
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;

	uint16_t field;
	memcpy(&field, ptr, sizeof(field));
	field = cio_be16toh(field);
	ws->read_frame_length = (uint64_t)field;
	get_mask_or_payload(ws, bs, buffer);
}

static void get_length64(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	size_t len = cio_read_buffer_get_transferred_bytes(buffer);
	if (unlikely((err != CIO_SUCCESS) || (len == 0))) {
		// TODO: fill out
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;

	uint64_t field;
	memcpy(&field, ptr, sizeof(field));
	field = cio_be64toh(field);
	ws->read_frame_length = field;
	get_mask_or_payload(ws, bs, buffer);
}

static void get_first_length(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	size_t len = cio_read_buffer_get_transferred_bytes(buffer);
	if (unlikely((err != CIO_SUCCESS) || (len == 0))) {
		// TODO: fill out
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	uint8_t field = *ptr;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;

	if ((field & WS_MASK_SET) == WS_MASK_SET) {
		ws->ws_flags.shall_mask = 1;
	} else {
		ws->ws_flags.shall_mask = 0;
	}

	field = field & ~WS_MASK_SET;
	if (field <= CIO_WEBSOCKET_SMALL_FRAME_SIZE) {
		ws->read_frame_length = (uint64_t)field;
		get_mask_or_payload(ws, bs, buffer);
	} else if (field == CIO_WEBSOCKET_SMALL_FRAME_SIZE + 1) {
		bs->read_exactly(bs, buffer, 2, get_length16, ws);
	} else {
		bs->read_exactly(bs, buffer, 8, get_length64, ws);
	}
}

static void get_header(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (unlikely(err != CIO_SUCCESS)) {
		if (err == CIO_EOF) {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "connection closed by other peer");
		} else {
			handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while reading websocket header");
		}

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
	uint8_t rsv_field;
	rsv_field = field & RSV_MASK;
	rsv_field = rsv_field >> 4;
	ws->ws_flags.rsv = rsv_field;

	static const uint8_t OPCODE_MASK = 0x0f;
	field = field & OPCODE_MASK;
	ws->ws_flags.opcode = field;
	err = bs->read_exactly(bs, buffer, 1, get_first_length, ws);
	if (err != CIO_SUCCESS) {
		handle_error(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "error while start reading websocket frame length");
	}
}

static void receive_frames(struct cio_websocket *ws)
{
	enum cio_error err = ws->bs->read_exactly(ws->bs, ws->rb, 1, get_header, ws);
	if (unlikely(err != CIO_SUCCESS)) {
		close(ws);
	}
}

static void handle_write(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	(void)bs;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	ws->write_handler(ws, ws->write_handler_context, buffer, err);
}

static void handle_write_ping(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	(void)bs;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	ws->write_ping_handler(ws, ws->write_ping_handler_context, buffer, err);
}

static enum cio_websocket_status write_binary_frame(struct cio_websocket *ws, struct cio_write_buffer *payload, bool last_frame, cio_websocket_write_handler handler, void *handler_context)
{
	ws->write_handler = handler;
	ws->write_handler_context = handler_context;
	send_frame(ws, payload, CIO_WEBSOCKET_BINARY_FRAME, last_frame, handle_write);
	if (unlikely(ws->ws_flags.to_be_closed)) {
		close(ws);
		return CIO_WEBSOCKET_STATUS_CLOSED;
	} else {
		return CIO_WEBSOCKET_STATUS_OK;
	}
}

static enum cio_websocket_status write_ping_frame(struct cio_websocket *ws, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context)
{
	if (unlikely(payload_size_in_limit(payload, CIO_WEBSOCKET_SMALL_FRAME_SIZE) == 0)) {
		return CIO_MESSAGE_TOO_LONG;
	}

	ws->write_ping_handler = handler;
	ws->write_ping_handler_context = handler_context;
	send_frame(ws, payload, CIO_WEBSOCKET_PING_FRAME, true, handle_write_ping);
	if (unlikely(ws->ws_flags.to_be_closed)) {
		close(ws);
		return CIO_WEBSOCKET_STATUS_CLOSED;
	} else {
		return CIO_WEBSOCKET_STATUS_OK;
	}
}

static enum cio_websocket_status write_text_frame(struct cio_websocket *ws, struct cio_write_buffer *payload, bool last_frame, cio_websocket_write_handler handler, void *handler_context)
{
	ws->write_handler = handler;
	ws->write_handler_context = handler_context;

	send_frame(ws, payload, CIO_WEBSOCKET_TEXT_FRAME, last_frame, handle_write);
	if (unlikely(ws->ws_flags.to_be_closed)) {
		close(ws);
		return CIO_WEBSOCKET_STATUS_CLOSED;
	} else {
		return CIO_WEBSOCKET_STATUS_OK;
	}
}

static void internal_on_connect(struct cio_websocket *ws)
{
	if (ws->on_connect != NULL) {
		ws->on_connect(ws);
	}

	if (unlikely(ws->ws_flags.to_be_closed == 1)) {
		close(ws);
	} else {
		receive_frames(ws);
	}
}

void cio_websocket_init(struct cio_websocket *ws, bool is_server, cio_websocket_close_hook close_hook)
{
	ws->internal_on_connect = internal_on_connect;
	ws->on_connect = NULL;
	ws->on_error = NULL;
	ws->on_close = NULL;
	ws->on_pong = NULL;
	ws->on_ping = NULL;
	ws->on_textframe = NULL;
	ws->on_binaryframe = NULL;
	ws->close = self_close_frame;
	ws->write_binaryframe = write_binary_frame;
	ws->write_pingframe = write_ping_frame;
	ws->write_textframe = write_text_frame;
	ws->close_hook = close_hook;
	ws->ws_flags.is_server = is_server ? 1 : 0;
	ws->ws_flags.is_fragmented = 0;
	ws->ws_flags.self_initiated_close = 0;
	ws->ws_flags.to_be_closed = 0;
	ws->ws_flags.writing_frame = 0;
	ws->ws_flags.handle_frame_ctx = 0;

	cio_write_buffer_element_init(&ws->wb_close_status, &ws->close_status, sizeof(ws->close_status));
}
