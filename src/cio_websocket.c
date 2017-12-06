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
#include "cio_random.h"
#include "cio_util.h"
#include "cio_websocket.h"

static const uint8_t WS_MASK_SET = 0x80;
static const uint8_t WS_HEADER_FIN = 0x80;
static const unsigned int WS_SMALL_FRAME_SIZE = 125;
static const unsigned int WS_MID_FRAME_SIZE = 65535;

static const uint64_t close_timeout_ns = UINT64_C(10) * UINT64_C(1000) * UINT64_C(1000);

enum cio_ws_frame_type {
	CIO_WEBSOCKET_CONTINUATION_FRAME = 0x0,
	CIO_WEBSOCKET_TEXT_FRAME = 0x1,
	CIO_WEBSOCKET_BINARY_FRAME = 0x2,
	CIO_WEBSOCKET_CLOSE_FRAME = 0x8,
	CIO_WEBSOCKET_PING_FRAME = 0x9,
	CIO_WEBSOCKET_PONG_FRAME = 0x0a,
};

static void receive_frames(struct cio_websocket *ws);

static void mask_payload(uint8_t *buffer, size_t length, uint8_t *mask)
{
	size_t bytewidth = sizeof(uint_fast16_t);
	if (length < 8) {
		bytewidth = 1;
	}

	size_t shift = 1;
	if (bytewidth > 2) {
		shift = 2;
	}

	if (bytewidth > 4) {
		shift = 3;
	}

	size_t pre_length, main_length, post_length;
	void *ptr_aligned;
	uint32_t mask32;

	switch (bytewidth) {
	case 8:
		pre_length = ((size_t) buffer) % bytewidth;
		pre_length = bytewidth - pre_length;
		main_length = (length - pre_length) >> shift;
		post_length = length - pre_length - (main_length << shift);
		ptr_aligned = buffer + pre_length;

		mask32 = 0x0;
		for (unsigned int i = 0; i < 4; i++) {
			mask32 |= (((uint32_t) *(mask + (i + pre_length) % 4)) & 0xFF) << (i * 8);
		}

		for (size_t i = 0; i < pre_length; i++) {
			buffer[i] ^= (mask[i % 4]);
		}

		uint64_t mask64 = ((uint64_t) mask32) & 0xFFFFFFFF;
		mask64 |= (mask64 << 32) & 0xFFFFFFFF00000000;
		uint64_t *buffer_aligned64 = ptr_aligned;
		for (size_t i = 0; i < main_length; i++) {
			buffer_aligned64[i] ^= mask64;
		}

		for (size_t i = length - post_length; i < length; i++) {
			buffer[i] ^= (mask[i % 4]);
		}

		break;

	case 4:
		pre_length = ((size_t) buffer) % bytewidth;
		pre_length = bytewidth - pre_length;
		main_length = (length - pre_length) >> shift;
		post_length = length - pre_length - (main_length << shift);
		ptr_aligned = buffer + pre_length;

		mask32 = 0x0;
		for (unsigned int i = 0; i < 4; i++) {
			mask32 |= (((uint32_t) *(mask + (i + pre_length) % 4)) & 0xFF) << (i * 8);
		}

		for (size_t i = 0; i < pre_length; i++) {
			buffer[i] ^= (mask[i % 4]);
		}

		uint32_t *buffer_aligned32 = ptr_aligned;
		for (size_t i = 0; i < main_length; i++) {
			buffer_aligned32[i] ^= mask32;
		}

		for (size_t i = length - post_length; i < length; i++) {
			buffer[i] ^= (mask[i % 4]);
		}

		break;

	default:
		for (size_t i = 0; i < length; i++) {
			buffer[i] = buffer[i] ^ (mask[i % 4]);
		}

		break;
	}
}

static void send_frame(struct cio_websocket *s, uint8_t *payload, size_t length, enum cio_ws_frame_type frame_type, cio_buffered_stream_write_handler written_cb)
{
	uint8_t first_len;
	size_t header_index = 2;

	s->send_header[0] = (uint8_t)(frame_type | WS_HEADER_FIN);
	if (length <= WS_SMALL_FRAME_SIZE) {
		first_len = (uint8_t)length;
	} else if (length <= WS_MID_FRAME_SIZE) {
		uint16_t be_len = cio_htobe16((uint16_t)length);
		memcpy(&s->send_header[2], &be_len, sizeof(be_len));
		header_index += sizeof(be_len);
		first_len = WS_SMALL_FRAME_SIZE + 1;
	} else {
		uint64_t be_len = cio_htobe64((uint64_t)length);
		memcpy(&s->send_header[2], &be_len, sizeof(be_len));
		header_index += sizeof(be_len);
		first_len = WS_SMALL_FRAME_SIZE + 2;
	}

	if (!s->is_server) {
		first_len |= WS_MASK_SET;
		uint8_t mask[4];
		cio_random_get_bytes(mask, sizeof(mask));
		memcpy(&s->send_header[header_index], &mask, sizeof(mask));
		header_index += sizeof(mask);
		mask_payload(payload, length, mask);
	}

	s->send_header[1] = first_len;

	cio_write_buffer_head_init(&s->wbh);
	cio_write_buffer_element_init(&s->wb_send_header, s->send_header, header_index);
	cio_write_buffer_element_init(&s->wb_send_payload, payload, length);
	cio_write_buffer_queue_tail(&s->wbh, &s->wb_send_header);
	cio_write_buffer_queue_tail(&s->wbh, &s->wb_send_payload);

	s->bs->write(s->bs, &s->wbh, written_cb, s);
}

static void close(struct cio_websocket *ws)
{
	if (ws->close_hook) {
		ws->close_hook(ws);
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
	if (ws->self_initiated_close) {
		receive_frames(ws);
	} else {
		close(ws);
	}
}

static void close_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;
	(void)err;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	close(ws);
}

static void send_close_frame(struct cio_websocket *ws, enum cio_websocket_status_code status_code)
{
	ws->close_status = cio_htobe16(status_code);
	cio_timer_init(&ws->close_timer, ws->loop, NULL);
	ws->close_timer.expires_from_now(&ws->close_timer, close_timeout_ns, close_timeout_handler, ws);
	send_frame(ws, (uint8_t *)&ws->close_status, sizeof(ws->close_status), CIO_WEBSOCKET_CLOSE_FRAME, close_frame_written);
}

static void handle_frame(struct cio_websocket *ws, uint8_t *data, uint64_t length)
{
	if (unlikely(ws->is_server && (ws->ws_flags.mask == 0))) {
		// TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		return;
	}

	if (ws->ws_flags.mask != 0) {
		mask_payload(data, length, ws->mask);
	}

	if (unlikely((ws->ws_flags.fin == 0) && (ws->ws_flags.opcode >= CIO_WEBSOCKET_CLOSE_FRAME))) {
		// TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		return;
	}

	if (ws->ws_flags.fin == 0) {
		if (ws->ws_flags.opcode != CIO_WEBSOCKET_CONTINUATION_FRAME) {
			if (unlikely(ws->ws_flags.is_fragmented)) {
				// TODO: log_err("Overwriting Opcode of unfinished fragmentation!");
				// TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
				return;
			}

			ws->ws_flags.is_fragmented = 1;
			ws->ws_flags.frag_opcode = ws->ws_flags.opcode;
			ws->ws_flags.opcode = CIO_WEBSOCKET_CONTINUATION_FRAME;
		} else {
			if (unlikely(ws->ws_flags.rsv != 0)) {
				// TODO:log_err("RSV-Bits must be 0 during continuation!");
				// TODO:handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
				return;
			}

			if (unlikely(!(ws->ws_flags.is_fragmented))) {
				//TODO: log_err("No start frame was send!");
				//TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
				return;
			}
		}
	}

	if (unlikely(ws->ws_flags.is_fragmented && ((ws->ws_flags.opcode < CIO_WEBSOCKET_CLOSE_FRAME) && (ws->ws_flags.opcode > CIO_WEBSOCKET_CONTINUATION_FRAME)))) {
		// log_err("Opcode during fragmentation must be 0x0!");
		// handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		return;
	}

	bool last_frame = false;
	unsigned int opcode;
	if (ws->ws_flags.opcode == CIO_WEBSOCKET_CONTINUATION_FRAME) {
		opcode = ws->ws_flags.frag_opcode;
		if (unlikely(ws->ws_flags.fin ==1)) {
			last_frame = true;
		}
	} else {
		opcode = ws->ws_flags.opcode;
	}

	switch (opcode) {
	case CIO_WEBSOCKET_BINARY_FRAME:
		if (likely(ws->onbinaryframe_received != NULL)) {
			ws->onbinaryframe_received(ws, data, length, last_frame);
		} else {
			// TODO: handle_error(s, WS_CLOSE_UNSUPPORTED);
		}

		break;

	case CIO_WEBSOCKET_TEXT_FRAME:
		if (likely(ws->on_textframe_received != NULL)) {
			ws->on_textframe_received(ws, (char *)data, length, last_frame);
		} else {
			// TODO: handle_error(s, WS_CLOSE_UNSUPPORTED);
		}

		break;

	case CIO_WEBSOCKET_PING_FRAME:
		if (unlikely(length > WS_SMALL_FRAME_SIZE)) {
			//TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		} else {
			// TODO: send_pong_frame(ws, data, length);
			if (ws->on_ping != NULL) {
				ws->on_ping(ws, data, length);
			}
		}

		break;

	case CIO_WEBSOCKET_PONG_FRAME:
		if (unlikely(length > WS_SMALL_FRAME_SIZE)) {
			//TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		} else {
			if (ws->on_pong != NULL) {
				ws->on_pong(ws, data, length);
			}
		}

		break;

	case CIO_WEBSOCKET_CLOSE_FRAME: {
		uint16_t status_code = CIO_WEBSOCKET_CLOSE_NORMAL;
		if (length >= 2) {
			memcpy(&status_code, data, sizeof(status_code));
			status_code = cio_be16toh(status_code);
		}

		if (length > 2) {
			// TODO: struct cjet_utf8_checker c;
			// TODO: cjet_init_checker(&c);
			// TODO: if (!cjet_is_byte_sequence_valid(&c, frame + 2, length - 2, true)) {
			// TODO: 	handle_error(s, WS_CLOSE_UNSUPPORTED_DATA);
			// TODO: }
		}

		if ((length == 1) || (length > WS_SMALL_FRAME_SIZE) || is_status_code_invalid(status_code)) {
			//handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		}

		if (ws->self_initiated_close) {
			ws->close_timer.cancel(&ws->close_timer);
		} else {
			if (ws->on_close != NULL) {
				ws->on_close(ws, (enum cio_websocket_status_code)status_code, NULL);
			}

			send_close_frame(ws, CIO_WEBSOCKET_CLOSE_GOING_AWAY);
		}

		break;
	}

	default:
		// TODO: handle_error(s, WS_CLOSE_PROTOCOL_ERROR);
		break;

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
		bs->read_exactly(bs, buffer, ws->read_frame_length, get_payload, ws);
	} else {
		buffer->bytes_transferred = 0;
		handle_frame(ws, NULL, 0);
	}
}

static void get_mask_or_payload(struct cio_websocket *ws, struct cio_buffered_stream *bs, struct cio_read_buffer *buffer)
{
	if (ws->ws_flags.mask == 1) {
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
		ws->ws_flags.mask = 1;
	} else {
		ws->ws_flags.mask = 0;
	}

	field = field & ~WS_MASK_SET;
	if (field < WS_SMALL_FRAME_SIZE + 1) {
		ws->read_frame_length = (uint64_t)field;
		get_mask_or_payload(ws, bs, buffer);
	} else if (field == WS_SMALL_FRAME_SIZE + 1) {
		bs->read_exactly(bs, buffer, 2, get_length16, ws);
	} else {
		bs->read_exactly(bs, buffer, 8, get_length64, ws);
	}
}

static void get_header(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	size_t len = cio_read_buffer_get_transferred_bytes(buffer);
	if (unlikely((err != CIO_SUCCESS) || (len == 0))) {
		// TODO: fill out
		return;
	}

	uint8_t *ptr = cio_read_buffer_get_read_ptr(buffer);
	uint8_t field = *ptr;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;

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
	bs->read_exactly(bs, buffer, 1, get_first_length, ws);
}

static void receive_frames(struct cio_websocket *ws)
{
	ws->bs->read_exactly(ws->bs, ws->rb, 1, get_header, ws);
}

static void self_close_frame(struct cio_websocket *ws, enum cio_websocket_status_code status_code)
{
	ws->self_initiated_close = true;
	send_close_frame(ws, status_code);
}

void cio_websocket_init(struct cio_websocket *ws, bool is_server, cio_websocket_close_hook close_hook)
{
	ws->onconnect_handler = NULL;
	ws->close = self_close_frame;
	ws->is_server = is_server;
	ws->close_hook = close_hook;
	ws->ws_flags.is_fragmented = 0;
}
