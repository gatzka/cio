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

enum cio_ws_frame_type {
	CIO_WEBSOCKET_CONTINUATION_FRAME = 0x0,
	CIO_WEBSOCKET_TEXT_FRAME = 0x1,
	CIO_WEBSOCKET_BINARY_FRAME = 0x2,
	CIO_WEBSOCKET_CLOSE_FRAME = 0x8,
	CIO_WEBSOCKET_PING_FRAME = 0x9,
	CIO_WEBSOCKET_PONG_FRAME = 0x0a,
};

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

static void close_frame_written(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	(void)bs;
	(void)buffer;
	(void)err;
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	close(ws);
}

static void send_close_frame(struct cio_websocket *ws, enum cio_websocket_status_code status_code)
{
	ws->close_status = cio_htobe16(status_code);
	// TODO: start timer for close frame written and probably waiting for close response
	send_frame(ws, (uint8_t *)&ws->close_status, sizeof(ws->close_status), CIO_WEBSOCKET_CLOSE_FRAME, close_frame_written);
}

static void self_close_going_away(struct cio_websocket *ws)
{
	ws->wait_for_close_response = true;
	send_close_frame(ws, CIO_WEBSOCKET_CLOSE_GOING_AWAY);
}

void cio_websocket_init(struct cio_websocket *ws, bool is_server, cio_websocket_close_hook close_hook)
{
	ws->onconnect_handler = NULL;
	ws->close = self_close_going_away;
	ws->is_server = is_server;
	ws->close_hook = close_hook;
}
