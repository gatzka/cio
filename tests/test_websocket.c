/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2018> <Stephan Gatzka>
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

#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cio/buffered_stream.h"
#include "cio/endian.h"
#include "cio/error_code.h"
#include "cio/eventloop.h"
#include "cio/random.h"
#include "cio/timer.h"
#include "cio/websocket.h"
#include "cio/websocket_masking.h"

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

static struct cio_websocket *ws;
static struct cio_http_client http_client;

enum frame_direction {
	FROM_CLIENT,
	FROM_SERVER
};

static void read_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err, size_t frame_length, uint8_t *data, size_t length, bool last_chunk, bool last_frame, bool is_binary);
FAKE_VOID_FUNC(read_handler, struct cio_websocket *, void *, enum cio_error, size_t, uint8_t *, size_t, bool, bool, bool)

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook_t)
FAKE_VALUE_FUNC(enum cio_error, cio_timer_cancel, struct cio_timer *)
FAKE_VOID_FUNC(cio_timer_close, struct cio_timer *)
FAKE_VALUE_FUNC(enum cio_error, cio_timer_expires_from_now, struct cio_timer *, uint64_t, cio_timer_handler_t, void *)

FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_read_at_least, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_read_at_most, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *)

typedef enum cio_error (*read_at_least_fake_fun)(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *);

FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler_t, void *)

static void on_connect(struct cio_websocket *s);
FAKE_VOID_FUNC(on_connect, struct cio_websocket *)

static void on_control(const struct cio_websocket *ws, enum cio_websocket_frame_type type, const uint8_t *data, uint_fast8_t length);
FAKE_VOID_FUNC(on_control, const struct cio_websocket *, enum cio_websocket_frame_type, const uint8_t *, uint_fast8_t)

static void on_error(const struct cio_websocket *ws, enum cio_error err, const char *reason);
FAKE_VOID_FUNC(on_error, const struct cio_websocket *, enum cio_error, const char *)

static void close_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err);
FAKE_VOID_FUNC(close_handler, struct cio_websocket *, void *, enum cio_error)

static void write_handler(struct cio_websocket *ws, void *context, enum cio_error err);
FAKE_VOID_FUNC(write_handler, struct cio_websocket *, void *, enum cio_error)

FAKE_VALUE_FUNC(enum cio_error, cio_random_seed_rng, cio_rng_t *)

FAKE_VALUE_FUNC(uint8_t, cio_check_utf8, struct cio_utf8_state *, const uint8_t *, size_t)
FAKE_VOID_FUNC(cio_utf8_init, struct cio_utf8_state *)

uint16_t cio_be16toh(uint16_t big_endian_16bits)
{
	return big_endian_16bits;
}

uint16_t cio_htobe16(uint16_t big_endian_16bits)
{
	return big_endian_16bits;
}

uint64_t cio_be64toh(uint64_t big_endian_64bits)
{
	return big_endian_64bits;
}

uint64_t cio_htobe64(uint64_t big_endian_64bits)
{
	return big_endian_64bits;
}

void cio_random_get_bytes(cio_rng_t *rng, void *bytes, size_t num_bytes)
{
	(void)rng;
	uint8_t *b = bytes;
	for (size_t i = 0; i < num_bytes; i++) {
		b[i] = (uint8_t)i;
	}
}

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define CIO_MIN(x, y) (((x) < (y)) ? (x) : (y))

#define WS_HEADER_FIN 0x80
#define WS_HEADER_RSV 0x70
#define WS_CLOSE_FRAME 0x8
#define WS_MASK_SET 0x80
#define WS_PAYLOAD_LENGTH 0x7f

struct ws_frame {
	enum cio_websocket_frame_type frame_type;
	enum frame_direction direction;
	const void *data;
	size_t data_length;
	bool last_frame;
	bool rsv;
};

static uint8_t frame_buffer[140000];
static size_t frame_buffer_read_pos = 0;
static size_t frame_buffer_fill_pos = 0;
static uint8_t read_buffer[140000];
static uint8_t read_back_buffer[140000];
static size_t read_back_buffer_pos = 0;
static uint8_t write_buffer[140000];
static size_t write_buffer_pos = 0;
static size_t write_buffer_parse_pos = 0;

static struct cio_buffered_stream *write_later_bs;
static struct cio_write_buffer *write_later_buf;
static cio_buffered_stream_write_handler_t write_later_handler;
static void *write_later_handler_context;

static enum cio_error timer_expires_from_now_save(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler_t handler, void *handler_context)
{
	(void)timeout_ns;
	t->handler = handler;
	t->handler_context = handler_context;
	return CIO_SUCCESS;
}

static void timer_close_cancel(struct cio_timer *t)
{
	if (t->handler) {
		t->handler(t, t->handler_context, CIO_OPERATION_ABORTED);
	}
}

static bool check_frame(enum cio_websocket_frame_type opcode, const char *payload, size_t payload_length, bool is_last_frame)
{
	uint8_t header = write_buffer[write_buffer_parse_pos++];
	if (((header & WS_HEADER_FIN) == WS_HEADER_FIN) != is_last_frame) {
		return false;
	}

	if ((header & WS_HEADER_RSV) != 0) {
		return false;
	}

	if ((header & opcode) != opcode) {
		return false;
	}

	uint8_t first_length = write_buffer[write_buffer_parse_pos++];
	bool is_masked = ((first_length & WS_MASK_SET) == WS_MASK_SET);
	first_length = first_length & (uint8_t)~WS_MASK_SET;

	uint64_t length;
	if (first_length == 126) {
		uint16_t len;
		memcpy(&len, &write_buffer[write_buffer_parse_pos], sizeof(len));
		write_buffer_parse_pos += sizeof(len);
		len = cio_be16toh(len);
		length = len;
	} else if (first_length == 127) {
		uint64_t len;
		memcpy(&len, &write_buffer[write_buffer_parse_pos], sizeof(len));
		write_buffer_parse_pos += sizeof(len);
		len = cio_be64toh(len);
		length = len;
	} else {
		length = first_length;
	}

	if (length != payload_length) {
		return false;
	}

	if (is_masked) {
		uint8_t mask[4];
		memcpy(mask, &write_buffer[write_buffer_parse_pos], sizeof(mask));
		write_buffer_parse_pos += sizeof(mask);
		cio_websocket_mask(&write_buffer[write_buffer_parse_pos], (size_t)length, mask);
	}

	if (length > SIZE_MAX) {
		return false;
	}

	if (length > 0) {
		if (memcmp(&write_buffer[write_buffer_parse_pos], payload, (size_t)length) != 0) {
			return false;
		}
	}

	write_buffer_parse_pos += payload_length;

	return true;
}

static bool is_close_frame(uint16_t status_code, bool status_code_required)
{
	if ((write_buffer[write_buffer_parse_pos] & WS_HEADER_FIN) != WS_HEADER_FIN) {
		return false;
	}

	if ((write_buffer[write_buffer_parse_pos++] & WS_CLOSE_FRAME) == 0) {
		return false;
	}

	uint8_t first_length = write_buffer[write_buffer_parse_pos++];
	bool is_masked = ((first_length & WS_MASK_SET) == WS_MASK_SET);
	first_length = first_length & (uint8_t)~WS_MASK_SET;
	if (first_length > CIO_WEBSOCKET_SMALL_FRAME_SIZE) {
		return false;
	}

	if ((first_length == 0) && (status_code_required)) {
		return false;
	}

	if (first_length == 1) {
		return false;
	}

	if (first_length > 0) {
		if (is_masked) {
			uint8_t mask[4];
			memcpy(mask, &write_buffer[write_buffer_parse_pos], sizeof(mask));
			write_buffer_parse_pos += sizeof(mask);
			cio_websocket_mask(&write_buffer[write_buffer_parse_pos], first_length, mask);
		}

		uint16_t sc;
		memcpy(&sc, &write_buffer[write_buffer_parse_pos], sizeof(sc));
		sc = cio_be16toh(sc);
		write_buffer_parse_pos += sizeof(sc);
		first_length = (uint8_t)(first_length - sizeof(sc));
		write_buffer_parse_pos += first_length;
		if (status_code != sc) {
			return false;
		}
	}

	return true;
}

static void serialize_frames(struct ws_frame frames[], size_t num_frames)
{
	uint32_t buffer_pos = 0;
	for (size_t i = 0; i < num_frames; i++) {
		struct ws_frame frame = frames[i];
		if (frame.last_frame) {
			frame_buffer[buffer_pos] = WS_HEADER_FIN;
		} else {
			frame_buffer[buffer_pos] = 0x0;
		}

		if (frame.rsv) {
			frame_buffer[buffer_pos] |= 0x70;
		}

		frame_buffer[buffer_pos] = (uint8_t)(frame_buffer[buffer_pos] | frame.frame_type);
		buffer_pos++;

		if (frame.direction == FROM_CLIENT) {
			frame_buffer[buffer_pos] = WS_MASK_SET;
		} else {
			frame_buffer[buffer_pos] = 0x00;
		}

		if (frame.data_length <= 125) {
			frame_buffer[buffer_pos] = (uint8_t)((unsigned int)frame_buffer[buffer_pos] | (unsigned int)frame.data_length);
			buffer_pos++;
		} else if (frame.data_length < 65536) {
			uint16_t len = (uint16_t)frame.data_length;
			frame_buffer[buffer_pos] |= 126;
			buffer_pos++;
			len = cio_htobe16(len);
			memcpy(&frame_buffer[buffer_pos], &len, sizeof(len));
			buffer_pos += (uint32_t)sizeof(len);
		} else {
			frame_buffer[buffer_pos] |= 127;
			buffer_pos++;
			uint64_t len = (uint64_t)frame.data_length;
			len = cio_htobe64(len);
			memcpy(&frame_buffer[buffer_pos], &len, sizeof(len));
			buffer_pos += (uint32_t)sizeof(len);
		}

		uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
		if (frame.direction == FROM_CLIENT) {
			memcpy(&frame_buffer[buffer_pos], mask, sizeof(mask));
			buffer_pos += (uint32_t)sizeof(mask);
		}

		if (frame.data_length > 0) {
			memcpy(&frame_buffer[buffer_pos], frame.data, frame.data_length);
			if (frame.direction == FROM_CLIENT) {
				cio_websocket_mask(&frame_buffer[buffer_pos], frame.data_length, mask);
			}

			buffer_pos += (uint32_t)frame.data_length;
		}
	}

	frame_buffer_fill_pos = buffer_pos - 1;
}

static enum cio_error bs_read_at_least_from_buffer(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	if (frame_buffer_read_pos > frame_buffer_fill_pos) {
		handler(bs, handler_context, CIO_EOF, buffer, num);
	} else {
		memcpy(buffer->add_ptr, &frame_buffer[frame_buffer_read_pos], num);
		buffer->add_ptr += num;
		frame_buffer_read_pos += num;

		handler(bs, handler_context, CIO_SUCCESS, buffer, num);
	}

	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_block(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;
	(void)handler;
	(void)handler_context;
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_peer_close(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	(void)num;
	handler(bs, handler_context, CIO_EOF, buffer, 0);
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	(void)num;
	handler(bs, handler_context, CIO_OPERATION_NOT_PERMITTED, buffer, 0);
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_immediate_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;
	(void)handler;
	(void)handler_context;

	return CIO_ADDRESS_IN_USE;
}

static enum cio_error bs_read_at_most_from_buffer(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	if (frame_buffer_read_pos > frame_buffer_fill_pos) {
		handler(bs, handler_context, CIO_EOF, buffer, num);
	} else {
		memcpy(buffer->add_ptr, &frame_buffer[frame_buffer_read_pos], num);
		buffer->add_ptr += num;
		frame_buffer_read_pos += num;

		handler(bs, handler_context, CIO_SUCCESS, buffer, num);
	}
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_most_from_buffer_parts(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	if (frame_buffer_read_pos > frame_buffer_fill_pos) {
		handler(bs, handler_context, CIO_EOF, buffer, num);
	} else {
		if (cio_buffered_stream_read_at_most_fake.call_count == 1) {
			num = num / 2;
		}

		memcpy(buffer->add_ptr, &frame_buffer[frame_buffer_read_pos], num);
		buffer->add_ptr += num;
		frame_buffer_read_pos += num;

		handler(bs, handler_context, CIO_SUCCESS, buffer, num);
	}
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_most_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	(void)num;
	handler(bs, handler_context, CIO_OPERATION_NOT_PERMITTED, buffer, 0);
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_most_immediate_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;
	(void)handler;
	(void)handler_context;

	return CIO_ADDRESS_IN_USE;
}

static enum cio_error bs_read_at_most_peer_close(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	(void)num;
	handler(bs, handler_context, CIO_EOF, buffer, 0);
	return CIO_SUCCESS;
}

static void websocket_free(struct cio_websocket *s)
{
	free(s);
}

static enum cio_error bs_write_ok(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler_t handler, void *handler_context)
{
	size_t len = cio_write_buffer_get_num_buffer_elements(buf);
	for (unsigned int i = 0; i < len; i++) {
		buf = buf->next;
		memcpy(&write_buffer[write_buffer_pos], buf->data.element.data, buf->data.element.length);
		write_buffer_pos += buf->data.element.length;
	}

	handler(bs, handler_context, CIO_SUCCESS);
	return CIO_SUCCESS;
}

static enum cio_error bs_write_error(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler_t handler, void *handler_context)
{
	(void)bs;
	(void)buf;
	(void)handler;
	(void)handler_context;
	return CIO_MESSAGE_TOO_LONG;
}

static enum cio_error bs_write_later(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler_t handler, void *handler_context)
{
	write_later_bs = bs;
	write_later_buf = buf;
	write_later_handler = handler;
	write_later_handler_context = handler_context;
	return CIO_SUCCESS;
}

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook_t hook)
{
	(void)l;
	timer->handler = NULL;
	timer->close_hook = hook;
	return CIO_SUCCESS;
}

static enum cio_error cio_timer_init_fails(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook_t hook)
{
	(void)timer;
	(void)l;
	(void)hook;
	return CIO_INVALID_ARGUMENT;
}

static void read_handler_save_data(struct cio_websocket *websocket, void *handler_context, enum cio_error err, size_t frame_length, uint8_t *data, size_t chunk_length, bool last_chunk, bool last_frame, bool is_binary)
{
	(void)last_frame;
	(void)is_binary;
	(void)frame_length;
	(void)last_chunk;

	if (err == CIO_SUCCESS) {
		if (chunk_length > 0) {
			memcpy(&read_back_buffer[read_back_buffer_pos], data, chunk_length);
			read_back_buffer_pos += chunk_length;
		}

		err = cio_websocket_read_message(websocket, read_handler, handler_context);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");
	}
}

static void on_control_save_data(const struct cio_websocket *websocket, enum cio_websocket_frame_type type, const uint8_t *data, uint_fast8_t length)
{
	(void)websocket;
	(void)type;
	if (length > 0) {
		memcpy(&read_back_buffer[read_back_buffer_pos], data, length);
		read_back_buffer_pos += length;
	}
}

static void on_error_save_data(const struct cio_websocket *websocket, enum cio_error err, const char *reason)
{
	(void)websocket;
	(void)err;
	size_t free_space = sizeof(read_back_buffer) - read_back_buffer_pos;
	strncpy((char *)&read_back_buffer[read_back_buffer_pos], reason, free_space - 1);
	read_back_buffer_pos += strlen(reason);
}

static void bs_init(struct cio_buffered_stream *bs)
{
	bs->callback_is_running = 0;
	bs->shall_close = false;
	cio_write_buffer_head_init(&bs->wbh);
}

void setUp(void)
{
	FFF_RESET_HISTORY()

	RESET_FAKE(read_handler)

	RESET_FAKE(on_connect)
	RESET_FAKE(on_control)
	RESET_FAKE(on_error)

	RESET_FAKE(cio_buffered_stream_read_at_least)
	RESET_FAKE(cio_buffered_stream_read_at_most)
	RESET_FAKE(cio_buffered_stream_write)

	RESET_FAKE(cio_timer_init)
	RESET_FAKE(cio_timer_cancel)
	RESET_FAKE(cio_timer_close)
	RESET_FAKE(cio_timer_expires_from_now)

	RESET_FAKE(close_handler)
	RESET_FAKE(write_handler)

	RESET_FAKE(cio_utf8_init)
	RESET_FAKE(cio_check_utf8)

	cio_check_utf8_fake.return_val = CIO_UTF8_ACCEPT;

	cio_read_buffer_init(&http_client.rb, read_buffer, sizeof(read_buffer));
	ws = malloc(sizeof(*ws));
	cio_websocket_server_init(ws, on_connect, NULL);
	ws->ws_private.http_client = &http_client;
	cio_websocket_set_on_control_cb(ws, on_control);
	cio_websocket_set_on_error_cb(ws, on_error);

	bs_init(&http_client.bs);

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
	cio_timer_expires_from_now_fake.custom_fake = timer_expires_from_now_save;
	cio_timer_close_fake.custom_fake = timer_close_cancel;

	read_handler_fake.custom_fake = read_handler_save_data;
	on_control_fake.custom_fake = on_control_save_data;
	on_error_fake.custom_fake = on_error_save_data;

	cio_buffered_stream_read_at_least_fake.custom_fake = bs_read_at_least_from_buffer;
	cio_buffered_stream_read_at_most_fake.custom_fake = bs_read_at_most_from_buffer;
	cio_buffered_stream_write_fake.custom_fake = bs_write_ok;

	frame_buffer_read_pos = 0;
	frame_buffer_fill_pos = 0;

	memset(read_buffer, 0x00, sizeof(read_buffer));
	memset(read_back_buffer, 0x00, sizeof(read_back_buffer));
	read_back_buffer_pos = 0;
	write_buffer_pos = 0;
	write_buffer_parse_pos = 0;
	memset(write_buffer, 0x00, sizeof(write_buffer));

	write_later_bs = NULL;
	write_later_buf = NULL;
	write_later_handler = NULL;
	write_later_handler_context = NULL;
}

void tearDown(void)
{
	free(ws);
}

static void test_receive_unfragmented_frames(void)
{
	uint32_t frame_sizes[] = {0, 1, 5, 125, 126, 65535, 65536};
	unsigned int frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};
	enum frame_direction dirs[] = {FROM_CLIENT, FROM_SERVER};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(frame_types); j++) {
			for (unsigned int k = 0; k < ARRAY_SIZE(dirs); k++) {
				uint32_t frame_size = frame_sizes[i];
				unsigned int frame_type = frame_types[j];
				enum frame_direction dir = dirs[k];

				char *data = malloc(frame_size);
				memset(data, 'a', frame_size);
				struct ws_frame frames[] = {
				    {.frame_type = frame_type, .direction = dir, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
				    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = dir, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
				};

				serialize_frames(frames, ARRAY_SIZE(frames));

				ws->ws_private.ws_flags.is_server = (dir == FROM_CLIENT) ? 1 : 0;
				enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

				TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(2, read_handler_fake.call_count, "read_handler was not called often enough");

				unsigned int data_frame_call_count;
				if (read_handler_fake.arg_histories_dropped == 0) {
					TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_handler_fake.call_count - 1], "read_handler wast not called with CIO_EOF");
					data_frame_call_count = read_handler_fake.call_count - 1;
				} else {
					data_frame_call_count = FFF_ARG_HISTORY_LEN;
				}

				for (unsigned int l = 0; l < data_frame_call_count; l++) {
					TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_history[l], "websocket parameter of read_handler not correct");
					TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[l], "context of read handler not NULL");
					TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[l], "error parameter of read_handler not CIO_SUCCESS");
					TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(frame_size, read_handler_fake.arg3_history[l], "remaining length parameter of read_handler not less or equal to frame_size");
					TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(frame_size, read_handler_fake.arg5_history[l], "chunk length parameter of read_handler not less or equal to frame_size");
					TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg7_history[l], "last_frame parameter of read_handler not true");
					TEST_ASSERT_EQUAL_MESSAGE(frame_type == CIO_WEBSOCKET_BINARY_FRAME, read_handler_fake.arg8_history[l], "is_binary argument not not correct");
				}

				TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
				if (frame_size > 0) {
					TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, frame_size, "data in read_handler not correct");
				}

				TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was not called for last close frame");
				TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_val, "websocket parameter of control callback is NULL");
				TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "websocket parameter of control callback is NULL");
				TEST_ASSERT_NULL_MESSAGE(on_control_fake.arg2_val, "data parameter of control callback is not correct");
				TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.arg3_val, "data length parameter of control callback is not correct");

				if (data) {
					free(data);
				}

				free(ws);
				setUp();
			}
		}
	}
}

static void test_receive_unfragmented_frames_in_parts(void)
{
	uint32_t frame_sizes[] = {0, 1, 5, 125, 126, 65535, 65536};
	unsigned int frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};
	enum frame_direction dirs[] = {FROM_CLIENT, FROM_SERVER};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(frame_types); j++) {
			for (unsigned int k = 0; k < ARRAY_SIZE(dirs); k++) {
				cio_buffered_stream_read_at_most_fake.custom_fake = bs_read_at_most_from_buffer_parts;
				uint32_t frame_size = frame_sizes[i];
				unsigned int frame_type = frame_types[j];
				enum frame_direction dir = dirs[k];

				char *data = malloc(frame_size);
				memset(data, 'a', frame_size);
				struct ws_frame frames[] = {
				    {.frame_type = frame_type, .direction = dir, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
				    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = dir, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
				};

				serialize_frames(frames, ARRAY_SIZE(frames));

				ws->ws_private.ws_flags.is_server = (dir == FROM_CLIENT) ? 1 : 0;
				enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

				TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(2, read_handler_fake.call_count, "read_handler was not called often enough");

				unsigned int data_frame_call_count;
				if (read_handler_fake.arg_histories_dropped == 0) {
					TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_handler_fake.call_count - 1], "read_handler wast not called with CIO_EOF");
					data_frame_call_count = read_handler_fake.call_count - 1;
				} else {
					data_frame_call_count = FFF_ARG_HISTORY_LEN;
				}

				for (unsigned int l = 0; l < data_frame_call_count; l++) {
					TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_history[l], "websocket parameter of read_handler not correct");
					TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[l], "context of read handler not NULL");
					TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[l], "error parameter of read_handler not CIO_SUCCESS");
					TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(frame_size, read_handler_fake.arg3_history[l], "remaining length parameter of read_handler not less or equal to frame_size");
					TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(frame_size, read_handler_fake.arg5_history[l], "chunk length parameter of read_handler not less or equal to frame_size");
					TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg7_history[l], "last_frame parameter of read_handler not true");
					TEST_ASSERT_EQUAL_MESSAGE(frame_type == CIO_WEBSOCKET_BINARY_FRAME, read_handler_fake.arg8_history[l], "is_binary argument not not correct");
				}

				TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
				if (frame_size > 0) {
					TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, frame_size, "data in read_handler not correct");
				}

				TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was not called for last close frame");
				TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_val, "websocket parameter of control callback is NULL");
				TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "websocket parameter of control callback is NULL");
				TEST_ASSERT_NULL_MESSAGE(on_control_fake.arg2_val, "data parameter of control callback is not correct");
				TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.arg3_val, "data length parameter of control callback is not correct");

				if (data) {
					free(data);
				}

				free(ws);
				setUp();
			}
		}
	}
}

static void test_receive_fragmented_frames(void)
{
	uint32_t first_frame_sizes[] = {0, 1, 5, 125, 126, 65535, 65536};
	uint32_t second_frame_sizes[] = {0, 1, 5, 125, 126, 65535, 65536};
	unsigned int frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};
	enum frame_direction dirs[] = {FROM_CLIENT, FROM_SERVER};

	for (unsigned int i = 0; i < ARRAY_SIZE(first_frame_sizes); i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(second_frame_sizes); j++) {
			for (unsigned int k = 0; k < ARRAY_SIZE(frame_types); k++) {
				for (unsigned int l = 0; l < ARRAY_SIZE(dirs); l++) {
					unsigned int frame_type = frame_types[k];
					uint32_t first_frame_size = first_frame_sizes[i];
					uint32_t second_frame_size = second_frame_sizes[j];
					enum frame_direction dir = dirs[l];

					char *first_data = malloc(first_frame_size);
					memset(first_data, 'a', first_frame_size);

					char *last_data = malloc(second_frame_size);
					memset(last_data, 'b', second_frame_size);
					struct ws_frame frames[] = {
					    {.frame_type = frame_type, .direction = dir, .data = first_data, .data_length = first_frame_size, .last_frame = false},
					    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = dir, .data = last_data, .data_length = second_frame_size, .last_frame = true, .rsv = false},
					    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = dir, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
					};

					serialize_frames(frames, ARRAY_SIZE(frames));

					ws->ws_private.ws_flags.is_server = (dir == FROM_CLIENT) ? 1 : 0;
					enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
					TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

					TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(3, read_handler_fake.call_count, "read_handler was not called");
					unsigned int arg_history_counter = CIO_MIN(read_handler_fake.arg_history_len, read_handler_fake.call_count);
					for (unsigned int read_cnt = 0; read_cnt < arg_history_counter; read_cnt++) {
						TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_history[read_cnt], "websocket parameter of read_handler not correct");
						TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[read_cnt], "context parameter of read handler not NULL");
						if (read_cnt < read_handler_fake.call_count - 1) {
							TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[read_cnt], "error parameter of read_handler not CIO_SUCCESS");
							if (read_cnt == 0) {
								TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg7_history[read_cnt], "last_frame parameter of read_handler for first fragment not false");
							}
							TEST_ASSERT_EQUAL_MESSAGE((frame_type == CIO_WEBSOCKET_BINARY_FRAME), read_handler_fake.arg8_history[read_cnt], "is_binary parameter of read_handler for first fragment not correct");
						} else {
							TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_cnt], "err parameter of read_handler not correct");
						}
					}

					TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
					if (first_frame_size > 0) {
						TEST_ASSERT_EQUAL_MEMORY_MESSAGE(first_data, read_back_buffer, first_frame_size, "data in data/binaray frame callback not correct");
						if (second_frame_size > 0) {
							TEST_ASSERT_EQUAL_MEMORY_MESSAGE(last_data, &read_back_buffer[first_frame_size], second_frame_size, "data in data/binaray frame callback not correct");
						}
					}

					TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was not called for last close frame");
					TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_val, "websocket parameter of control callback is NULL");
					TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "websocket parameter of control callback is NULL");
					TEST_ASSERT_NULL_MESSAGE(on_control_fake.arg2_val, "data parameter of control callback is not correct");
					TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.arg3_val, "data length parameter of control callback is not correct");

					free(ws);
					free(first_data);
					free(last_data);
					setUp();
				}
			}
		}
	}
}

static void test_incoming_ping_pong_send_fails(void)
{
	struct cio_websocket *my_ws = malloc(sizeof(*my_ws));
	enum cio_error err = cio_websocket_server_init(my_ws, on_connect, websocket_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not init websocket");
	my_ws->ws_private.http_client = &http_client;
	cio_websocket_set_on_error_cb(my_ws, on_error);
	cio_websocket_set_on_control_cb(my_ws, on_control);

	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	cio_buffered_stream_write_fake.custom_fake = bs_write_error;

	serialize_frames(frames, ARRAY_SIZE(frames));

	err = cio_websocket_read_message(my_ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(my_ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(my_ws, on_error_fake.arg0_val, "websocket parameter of error handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, on_error_fake.arg1_val, "error code in error handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "on_control callback was not called once");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_val, "websocket parameter of control callback (ping) is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_PING_FRAME, on_control_fake.arg1_val, "type parameter of control callback (ping) is not PING");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_control_fake.arg3_val, "data length parameter of control callback (ping) is not correct");

	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "data in ping frame callback not correct");
}

static void test_close_close_response_fails(void)
{
	uint8_t data[] = {0xe8, 0x3, 'G', 'o', 'o', 'd', ' ', 'B', 'y', 'e'};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	cio_buffered_stream_write_fake.custom_fake = bs_write_error;

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of error handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, on_error_fake.arg1_val, "error code in error handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was called for last close frame");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_control_fake.arg0_val, "websocket parameter of on_control handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "frame type parameter of on_control handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_control_fake.arg3_val, "data length parameter of on_control handler not correct");

	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "echoed data in close frame not correct");
}

static void test_close_self_sendframe_fails(void)
{
	cio_buffered_stream_write_fake.custom_fake = bs_write_error;

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Close not failed correctly");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of error handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, on_error_fake.arg1_val, "error code in error handler not correct");
}

static void test_send_multiple_jobs_with_failures(void)
{
	char buffer[125] = {'a'};

	typedef enum cio_error (*write_func)(struct cio_buffered_stream * bs, struct cio_write_buffer * buffer, cio_buffered_stream_write_handler_t handler, void *handler_context);
	write_func write_funcs[3] = {bs_write_later, bs_write_error, bs_write_ok};

	cio_buffered_stream_write_fake.custom_fake_seq = write_funcs;
	cio_buffered_stream_write_fake.custom_fake_seq_len = ARRAY_SIZE(write_funcs);

	struct cio_write_buffer ping_wbh;
	cio_write_buffer_head_init(&ping_wbh);
	struct cio_write_buffer pong_wbh;
	cio_write_buffer_head_init(&pong_wbh);
	struct cio_write_buffer text_wbh;
	cio_write_buffer_head_init(&text_wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&text_wbh, &wb);

	enum cio_error err = cio_websocket_write_ping(ws, &ping_wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a ping frame did not succeed!");
	err = cio_websocket_write_pong(ws, &pong_wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a pong frame did not succeed!");
	err = cio_websocket_write_message_first_chunk(ws, cio_write_buffer_get_total_size(&text_wbh), &text_wbh, true, false, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a text frame did not succeed!");
	err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a close frame did not succeed!");

	// Simulate write call over the eventloop
	cio_buffered_stream_write_fake.custom_fake = bs_write_error;
	bs_write_ok(write_later_bs, write_later_buf, write_later_handler, write_later_handler_context);

	TEST_ASSERT_EQUAL_MESSAGE(4, write_handler_fake.call_count, "Write handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_history[0], "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_history[0], "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_history[0], "err parameter of write_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_history[1], "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_history[1], "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_ABORTED, write_handler_fake.arg2_history[1], "err parameter of write_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_history[2], "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_history[2], "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_ABORTED, write_handler_fake.arg2_history[2], "err parameter of write_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_history[3], "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_history[3], "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_ABORTED, write_handler_fake.arg2_history[3], "err parameter of write_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in first fragment of error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, on_error_fake.arg1_val, "error parameter in error callback not correct");

	TEST_ASSERT_TRUE_MESSAGE(check_frame(CIO_WEBSOCKET_PING_FRAME, NULL, 0, true), "Written ping frame not correct");
	TEST_ASSERT_TRUE_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, true), "Written close frame not correct");
}

static void test_immediate_read_error_for_get_header(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_immediate_read_error_for_get_first_length(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_immediate_read_error_for_get_mask(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_immediate_read_error_for_get_extended_length(void)
{
	uint32_t frame_sizes[] = {30000, 70000};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;

		uint32_t frame_size = frame_sizes[i];
		char *data = malloc(frame_size);
		memset(data, 'a', frame_size);
		struct ws_frame frames[] = {
		    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
		};

		serialize_frames(frames, ARRAY_SIZE(frames));
		enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_immediate_error};

		SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

		enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

		TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

		TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
		TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, on_error_fake.arg1_val, "error callback called with wrong status code");

		if (data) {
			free(data);
		}

		free(ws);
		setUp();
	}
}

static void test_immediate_read_error_for_get_payload(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	cio_buffered_stream_read_at_most_fake.custom_fake = bs_read_at_most_immediate_error;

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_immediate_read_error_for_second_get_payload(void)
{
	enum cio_websocket_frame_type frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_types); i++) {
		enum cio_websocket_frame_type frame_type = frame_types[i];

		char data[5];
		memset(data, 'a', sizeof(data));
		struct ws_frame frames[] = {
		    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
		};

		serialize_frames(frames, ARRAY_SIZE(frames));
		enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_immediate_error};

		SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

		enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

		TEST_ASSERT_EQUAL_MESSAGE(2, read_handler_fake.call_count, "read_handler was not called");
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

		TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
		TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, on_error_fake.arg1_val, "error callback called with wrong status code");

		free(ws);
		setUp();
	}
}

static void test_read_error_in_get_header(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	cio_buffered_stream_read_at_least_fake.custom_fake = bs_read_at_least_error;

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_NOT_PERMITTED, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_read_error_in_get_mask(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_error};

	SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_NOT_PERMITTED, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_read_error_in_get_payload(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[6];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	cio_buffered_stream_read_at_most_fake.custom_fake = bs_read_at_most_error;

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_NOT_PERMITTED, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_read_no_utf8(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	cio_buffered_stream_read_at_least_fake.custom_fake = bs_read_at_least_from_buffer;
	cio_check_utf8_fake.return_val = CIO_UTF8_REJECT;

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_receive_ping_frame(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(2, on_control_fake.call_count, "on_control callback was not called twice (for ping and close)");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_history[0], "websocket parameter of control callback (ping) is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_PING_FRAME, on_control_fake.arg1_history[0], "type parameter of control callback (ping) is not PING");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_control_fake.arg3_history[0], "data length parameter of control callback (ping) is not correct");

	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_history[1], "websocket parameter of control callback (close) is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_history[1], "type parameter of control callback (close) is not CLOSE");
	TEST_ASSERT_NULL_MESSAGE(on_control_fake.arg2_history[1], "data parameter of control callback (close) is not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.arg3_history[1], "data length parameter of control callback (close) is not correct");

	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "data in ping frame callback not correct");
}

static void test_receive_ping_frame_no_callback(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	cio_websocket_set_on_control_cb(ws, NULL);
	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
}

static void test_receive_ping_frame_no_payload(void)
{
	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(2, on_control_fake.call_count, "on_control callback was not called twice (for ping and close)");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_history[0], "websocket parameter of control callback (ping) is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_PING_FRAME, on_control_fake.arg1_history[0], "type parameter of control callback (ping) is not PING");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.arg3_history[0], "data length parameter of control callback (ping) is not correct");

	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_history[1], "websocket parameter of control callback (close) is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_history[1], "type parameter of control callback (close) is not CLOSE");
	TEST_ASSERT_NULL_MESSAGE(on_control_fake.arg2_history[1], "data parameter of control callback (close) is not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.arg3_history[1], "data length parameter of control callback (close) is not correct");
}

static void test_receive_ping_frame_payload_too_long(void)
{
	char data[126] = {'a'};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in first fragment of error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error parameter in error callback not correct");
	TEST_ASSERT_EQUAL_STRING_MESSAGE("payload of control frame too long", read_back_buffer, "reason in error callback not correct");
	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, true), "written frame is not a close frame!");
}

static void test_recieve_ping_frame_payload_too_long_no_error_callback(void)
{
	char data[126] = {'a'};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	cio_websocket_set_on_error_cb(ws, NULL);
	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, true), "written frame is not a close frame!");
}

static void test_receive_ping_frame_restart_receive_fails_after_pong_written(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
}

static void test_receive_pong_frame(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PONG_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(2, on_control_fake.call_count, "on_control callback was not called twice (for pong and close)");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_history[0], "websocket parameter of control callback (pong) is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_PONG_FRAME, on_control_fake.arg1_history[0], "type parameter of control callback (pong) is not PONG");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_control_fake.arg3_history[0], "data length parameter of control callback (pong) is not correct");

	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_history[1], "websocket parameter of control callback (close) is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_history[1], "type parameter of control callback (close) is not CLOSE");
	TEST_ASSERT_NULL_MESSAGE(on_control_fake.arg2_history[1], "data parameter of control callback (close) is not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.arg3_history[1], "data length parameter of control callback (close) is not correct");

	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "data in pong frame callback not correct");
}

static void test_receive_pong_frame_no_callback(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PONG_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	cio_websocket_set_on_control_cb(ws, NULL);
	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
}

static void test_receive_pong_frame_restart_receive_fails(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PONG_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
}

static void test_illegal_opcode(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PONG_FRAME + 1, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "number of read_handler callbacks called not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[read_handler_fake.call_count - 1], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_handler_fake.call_count - 1], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of on_error not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error code of on_error not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for close frame");
}

static void test_wrong_opcode_between_fragments(void)
{
	char data[5];
	memset(data, 'a', sizeof(data));

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(3, read_handler_fake.call_count, "read_handler was not called");

	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[2], "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[2], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[2], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in first fragment of error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "status parameter in error callback not correct");
	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, true), "written frame is not a close frame!");
}

static void test_wrong_opcode_in_fragment(void)
{
	char data[12];
	memset(data, 'a', sizeof(data));

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(3, read_handler_fake.call_count, "read_handler was not called");

	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[2], "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[2], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[2], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in first fragment of error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "status parameter in error callback not correct");
	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, true), "written frame is not a close frame!");
}

static void test_control_frame_within_fragment(void)
{
	char data[12];
	memset(data, 'a', sizeof(data));

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(4, read_handler_fake.call_count, "read_handler was not called");

	for (unsigned int i = 0; i < 3; i++) {
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[i], "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[i], "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[i], "err parameter of read_handler not correct");
		TEST_ASSERT_NOT_NULL_MESSAGE(read_handler_fake.arg4_history[i], "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), read_handler_fake.arg5_history[i], "length parameter of read_handler not correct");
		TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg8_history[i], "is_binary parameter of read_handler not correct");
		if (i < 2) {
			TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg7_history[i], "last_frame parameter of read_handler not correct");
		} else {
			TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg7_history[i], "last_frame parameter of read_handler not correct");
		}
	}

	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[3], "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[3], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[3], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(2, on_control_fake.call_count, "control callback was not called for last ping and close frame");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, on_control_fake.arg0_history[0], "websocket parameter of control callback is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_PING_FRAME, on_control_fake.arg1_history[0], "control frame type of control callback not correct");

	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, on_control_fake.arg0_history[1], "websocket parameter of control callback is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_history[1], "control frame type of control callback not correct");
}

static void test_binary_frame_within_text_fragments(void)
{
	char data[12];
	memset(data, 'a', sizeof(data));

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_BINARY_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(3, read_handler_fake.call_count, "number of read_handler callbacks called not correct");

	for (unsigned int i = 0; i < read_handler_fake.call_count - 1; i++) {
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[i], "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[i], "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[i], "err parameter of read_handler not correct");
		TEST_ASSERT_NOT_NULL_MESSAGE(read_handler_fake.arg4_history[i], "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), read_handler_fake.arg5_history[i], "length parameter of read_handler not correct");
		TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg7_history[i], "last_frame parameter of read_handler not correct");
		TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg8_history[i], "is_binary parameter of read_handler not correct");
	}

	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[read_handler_fake.call_count - 1], "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[read_handler_fake.call_count - 1], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_handler_fake.call_count - 1], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for close frame");
}

static void test_rsv_bit_in_header(void)
{
	char data[9];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = true},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_fragmented_control_frame(void)
{
	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = false, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_wrong_continuation_frame_without_correct_start_frame(void)
{
	char data[42];
	memset(data, 'a', sizeof(data));

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_wrong_fragment_start(void)
{
	char data[64];
	memset(data, 'a', sizeof(data));

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "number of read_handler callbacks called not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[read_handler_fake.call_count - 1], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_handler_fake.call_count - 1], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of on_error not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for close frame");
}

static void test_no_mask_set_from_client(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_SERVER, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = 1;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for last close frame");

	if (data) {
		free(data);
	}
}

static void test_close_self_no_answer(void)
{
	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	cio_buffered_stream_read_at_least_fake.custom_fake = bs_read_at_least_block;

	err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "read_message did not succeed");

	ws->ws_private.close_timer.handler(&ws->ws_private.close_timer, ws->ws_private.close_timer.handler_context, CIO_SUCCESS);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was not called for last close frame");
}

static void test_close_in_get_header(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	cio_buffered_stream_read_at_least_fake.custom_fake = bs_read_at_least_peer_close;

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_close_in_get_length(void)
{
	uint32_t frame_sizes[] = {100, 30000, 70000};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		uint32_t frame_size = frame_sizes[i];
		char *data = malloc(frame_size);
		memset(data, 'a', frame_size);
		struct ws_frame frames[] = {
		    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
		};

		serialize_frames(frames, ARRAY_SIZE(frames));

		read_at_least_fake_fun *read_at_least_fakes;
		if (frame_size <= 125) {
			read_at_least_fakes = malloc(2 * sizeof(read_at_least_fake_fun));
			read_at_least_fakes[0] = bs_read_at_least_from_buffer;
			read_at_least_fakes[1] = bs_read_at_least_peer_close;
			SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, 2)
		} else {
			read_at_least_fakes = malloc(3 * sizeof(read_at_least_fake_fun));
			read_at_least_fakes[0] = bs_read_at_least_from_buffer;
			read_at_least_fakes[1] = bs_read_at_least_from_buffer;
			read_at_least_fakes[2] = bs_read_at_least_peer_close;
			SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, 3)
		}

		enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

		TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

		TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
		TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, on_error_fake.arg1_val, "error callback called with wrong status code");

		if (data) {
			free(data);
		}

		free(read_at_least_fakes);

		free(ws);
		setUp();
	}
}

static void test_close_in_get_mask(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler_t, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_peer_close};

	SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes))

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_close_in_get_payload(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[6];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	cio_buffered_stream_read_at_most_fake.custom_fake = bs_read_at_most_peer_close;

	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_close_self_no_reason(void)
{
	uint8_t data[] = {0xe8, 0x3};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was not called");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was not called for last close frame");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_val, "websocket parameter of control callback is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "websocket parameter of control callback is NULL");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg2_val, "data parameter of control callback is not correct");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_control_fake.arg3_val, "data length parameter of control callback is not correct");
}

static void test_close_self_with_reason(void)
{
	uint8_t data[] = {0xe8, 0x3};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Closing did not succeed!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was not called");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was not called for last close frame");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_val, "websocket parameter of control callback is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "websocket parameter of control callback is NULL");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg2_val, "data parameter of control callback is not correct");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_control_fake.arg3_val, "data length parameter of control callback is not correct");
}

static void test_close_self_no_ws(void)
{
	enum cio_error err = cio_websocket_close(NULL, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Close not failed correctly");
}

static void test_close_self_no_handler(void)
{
	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Close not failed correctly");
}

static void test_close_self_twice(void)
{
	cio_buffered_stream_write_fake.custom_fake = bs_write_later;

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Close did not succeed");

	err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_NOT_PERMITTED, err, "Close did not failed correctly");
}

static void test_close_self_timer_init_fails(void)
{
	cio_timer_init_fake.custom_fake = cio_timer_init_fails;

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Close not failed correctly");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_timer_cancel_fake.call_count, "Timer cancel was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_timer_close_fake.call_count, "Timer close was called");
}

static void test_close_self_timer_expire_fails(void)
{
	cio_timer_expires_from_now_fake.custom_fake = NULL;
	cio_timer_expires_from_now_fake.return_val = CIO_ADDRESS_IN_USE;

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, err, "Close not failed correctly");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_timer_cancel_fake.call_count, "Timer cancel was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, cio_timer_close_fake.call_count, "Timer close was called");
}

static void test_close_self_without_read(void)
{
	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Close failed");

	ws->ws_private.close_timer.handler(&ws->ws_private.close_timer, ws->ws_private.close_timer.handler_context, CIO_SUCCESS);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was not called for last close frame");
}

static void test_close_self_without_close_hook(void)
{
	struct cio_websocket my_ws;
	enum cio_error err = cio_websocket_server_init(&my_ws, on_connect, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Init did not succeeded");

	my_ws.ws_private.http_client = &http_client;

	uint8_t data[] = {0xe8, 0x3};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	err = cio_websocket_close(&my_ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	err = cio_websocket_read_message(&my_ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(&my_ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was not called");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was not called for last close frame");
}

static void test_close_with_valid_status_codes(void)
{
	uint8_t close_codes[][2] = {{0xe8, 0x3}, {0xe9, 0x3}, {0xea, 0x3}, {0xeb, 0x3}, {0xef, 0x3}, {0xf3, 0x3}, {0xb8, 0xb}, {0x87, 0x13}};

	for (unsigned int i = 0; i < ARRAY_SIZE(close_codes); i++) {
		uint8_t *data = close_codes[i];
		struct ws_frame frames[] = {
		    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = 2, .last_frame = true, .rsv = false},
		};

		serialize_frames(frames, ARRAY_SIZE(frames));

		ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
		enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

		TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
		TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

		TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was not called");

		TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was called for last close frame");
		TEST_ASSERT_EQUAL_MESSAGE(ws, on_control_fake.arg0_val, "websocket parameter of on_control handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "frame type parameter of on_control handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(2, on_control_fake.arg3_val, "data length parameter of on_control handler not correct");

		TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, 2, "echoed data in close frame not correct");

		free(ws);
		setUp();
	}
}

static void test_close_reason_not_utf8(void)
{
	uint8_t data[] = {0xe8, 0x3, 0xf8, 0x88, 0x80, 0x80, 0x80};
	cio_check_utf8_fake.return_val = CIO_UTF8_REJECT;

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of error handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error code in error handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for last close frame");
}

static void test_text_frame_utf8_no_complete_in_last_frame(void)
{
	cio_check_utf8_fake.return_val = 11;
	uint8_t data[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2d, 0xc2, 0xb5, 0x40, 0xc3}; //9fc3b6c3a4
	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of error handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error code in error handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for last close frame");
}

static void test_close_short_status(void)
{
	char data[] = {0xa};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of error handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error code in error handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for last close frame");
}

static void test_close_with_invalid_status_codes(void)
{
	uint8_t close_codes[][2] = {{0xe7, 0x3}, {0xed, 0x3}, {0x88, 0x13}};

	for (unsigned int i = 0; i < ARRAY_SIZE(close_codes); i++) {
		uint8_t *data = close_codes[i];
		struct ws_frame frames[] = {
		    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = 2, .last_frame = true, .rsv = false},
		};

		serialize_frames(frames, ARRAY_SIZE(frames));

		ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
		enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

		TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
		TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

		TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");

		TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for last close frame");

		free(ws);
		setUp();
	}
}

static void test_send_pong_frame(void)
{
	char buffer[] = "aaaaaaaa";

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_pong(ws, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a pong frame did not succeed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "Write handler was not called once");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_val, "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_val, "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_val, "err parameter of write_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Length of write buffer different than before writing!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buffer, wbh.next->data.element.data, sizeof(buffer), "Content of writebuffer not correct after writing!");

	TEST_ASSERT_TRUE_MESSAGE(check_frame(CIO_WEBSOCKET_PONG_FRAME, buffer, sizeof(buffer), true), "Written pong frame not correct");
}

static void test_send_pong_frame_no_ws(void)
{
	char buffer[] = "aaaaaaaa";

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_pong(NULL, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Writing a pong frame did not succeed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "Write handler was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Length of write buffer different than before writing!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buffer, wbh.next->data.element.data, sizeof(buffer), "Content of writebuffer not correct after writing!");
}

static void test_send_ping_frame_no_ws(void)
{
	char buffer[] = "aaaaaaaa";

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_ping(NULL, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Writing a ping frame did not succeed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "Write handler was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Length of write buffer different than before writing!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buffer, wbh.next->data.element.data, sizeof(buffer), "Content of writebuffer not correct after writing!");
}

static void test_send_pong_frame_no_handler(void)
{
	char buffer[] = "aaaaaaaa";

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_pong(ws, &wbh, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Writing a pong frame did not succeed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "Write handler was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Length of write buffer different than before writing!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buffer, wbh.next->data.element.data, sizeof(buffer), "Content of writebuffer not correct after writing!");
}

static void test_send_pong_frame_payload_too_large(void)
{
	char buffer[126] = {'a'};

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_pong(ws, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Writing a pong frame did not succeed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "Write handler was called");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Length of write buffer different than before writing!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buffer, wbh.next->data.element.data, sizeof(buffer), "Content of writebuffer not correct after writing!");
}

static void test_send_pong_frame_twice(void)
{
	char buffer[125] = {'a'};

	cio_buffered_stream_write_fake.custom_fake = bs_write_later;

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_pong(ws, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a pong frame did not succeed!");

	err = cio_websocket_write_pong(ws, NULL, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_NOT_PERMITTED, err, "Writing a second pong frame did not failed");

	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "Write handler was not called once");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
}

static void test_send_ping_frame(void)
{
	char buffer[] = "aaaaaaaa";

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_ping(ws, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a ping frame did not succeed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "Write handler was not called once");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_val, "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_val, "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_val, "err parameter of write_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Length of write buffer different than before writing!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buffer, wbh.next->data.element.data, sizeof(buffer), "Content of writebuffer not correct after writing!");

	TEST_ASSERT_TRUE_MESSAGE(check_frame(CIO_WEBSOCKET_PING_FRAME, buffer, sizeof(buffer), true), "Written ping frame not correct");
}

static void test_send_text_binary_frame(void)
{
	uint32_t frame_sizes[] = {1, 5, 125, 126, 65535, 65536};
	unsigned int frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};
	enum frame_direction directions[] = {FROM_CLIENT, FROM_SERVER};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(frame_types); j++) {
			for (unsigned int k = 0; k < ARRAY_SIZE(directions); k++) {
				enum frame_direction direction = directions[k];
				uint32_t frame_size = frame_sizes[i];
				char *data = malloc(frame_size);
				memset(data, 'a', frame_size);
				char *check_data = malloc(frame_size);
				memcpy(check_data, data, frame_size);

				struct ws_frame frames[] = {
				    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = direction, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
				};

				serialize_frames(frames, ARRAY_SIZE(frames));

				struct cio_write_buffer wbh;
				cio_write_buffer_head_init(&wbh);

				struct cio_write_buffer wb;
				cio_write_buffer_element_init(&wb, data, frame_size);
				cio_write_buffer_queue_tail(&wbh, &wb);

				ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
				uint32_t context = 0x1234568;
				if (frame_types[j] == CIO_WEBSOCKET_TEXT_FRAME) {
					enum cio_error err = cio_websocket_write_message_first_chunk(ws, cio_write_buffer_get_total_size(&wbh), &wbh, true, false, write_handler, &context);
					TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a text frame did not succeed!");
					TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_TEXT_FRAME, check_data, frame_size, true), "First frame send is incorrect text frame!");
				} else if (frame_types[j] == CIO_WEBSOCKET_BINARY_FRAME) {
					enum cio_error err = cio_websocket_write_message_first_chunk(ws, cio_write_buffer_get_total_size(&wbh), &wbh, true, true, write_handler, &context);
					TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a binary frame did not succeed!");
					TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_BINARY_FRAME, check_data, frame_size, true), "First frame send is incorrect binary frame!");
				}

				enum cio_error err = cio_websocket_read_message(ws, read_handler, NULL);
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");
				TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "written frame is not a close frame!");

				TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write handler was not called!");
				TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, write_handler_fake.arg0_val, "websocket pointer in write handler not correct!");
				TEST_ASSERT_EQUAL_PTR_MESSAGE(&context, write_handler_fake.arg1_val, "context pointer in write handler not correct!");
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_val, "error code in write handler not correct!");

				TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Length of write buffer different than before writing!");
				TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
				TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, wbh.next->data.element.data, frame_size, "Content of writebuffer not correct after writing!");

				TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
				TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was called not for last close frame");

				if (data) {
					free(data);
				}

				if (check_data) {
					free(check_data);
				}

				free(ws);
				setUp();
			}
		}
	}
}

static void test_send_chunks(void)
{
	enum frame_direction directions[] = {FROM_CLIENT, FROM_SERVER};

	for (unsigned int k = 0; k < ARRAY_SIZE(directions); k++) {
		unsigned int frame_type = CIO_WEBSOCKET_BINARY_FRAME;
		enum frame_direction direction = directions[k];
		char data[] = "HelloWorld!";
		char check_data[] = "HelloWorld!";

		struct ws_frame frames[] = {
		    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = direction, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
		};

		serialize_frames(frames, ARRAY_SIZE(frames));

		struct cio_write_buffer wbh;
		cio_write_buffer_head_init(&wbh);

		struct cio_write_buffer wb;
		cio_write_buffer_element_init(&wb, data, sizeof(data) / 2);
		cio_write_buffer_queue_tail(&wbh, &wb);

		ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
		uint32_t context = 0x1234568;
		enum cio_error err = cio_websocket_write_message_first_chunk(ws, sizeof(data), &wbh, true, frame_type == CIO_WEBSOCKET_BINARY_FRAME, write_handler, &context);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a frame did not succeed!");

		cio_write_buffer_head_init(&wbh);
		cio_write_buffer_element_init(&wb, &data[sizeof(data) / 2], sizeof(data) - (sizeof(data) / 2));
		cio_write_buffer_queue_tail(&wbh, &wb);
		err = cio_websocket_write_message_continuation_chunk(ws, &wbh, write_handler, &context);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a frame did not succeed!");

		TEST_ASSERT_MESSAGE(check_frame(frame_type, check_data, sizeof(check_data), true), "First frame send is incorrect text frame!");

		err = cio_websocket_read_message(ws, read_handler, NULL);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");
		TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "written frame is not a close frame!");

		TEST_ASSERT_EQUAL_MESSAGE(2, write_handler_fake.call_count, "write handler was not called!");
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, write_handler_fake.arg0_val, "websocket pointer in write handler not correct!");
		TEST_ASSERT_EQUAL_PTR_MESSAGE(&context, write_handler_fake.arg1_val, "context pointer in write handler not correct!");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_val, "error code in write handler not correct!");

		TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
		TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was called not for last close frame");

		free(ws);
		setUp();
	}
}

struct fragmented_websocket {
	struct cio_websocket ws;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
	char first_fragment[5];
	char second_fragment[5];
	char third_fragment[5];
};

static void write_handler_third_fragment(struct cio_websocket *websocket, void *context, enum cio_error err)
{
	write_handler_fake.custom_fake = NULL;

	struct fragmented_websocket *fws = (struct fragmented_websocket *)context;

	TEST_ASSERT_TRUE_MESSAGE(check_frame(CIO_WEBSOCKET_CONTINUATION_FRAME, fws->second_fragment, sizeof(fws->second_fragment), false), "Second fragment written not correct");

	cio_write_buffer_head_init(&fws->wbh);
	cio_write_buffer_element_init(&fws->wb, fws->third_fragment, sizeof(fws->third_fragment));
	cio_write_buffer_queue_tail(&fws->wbh, &fws->wb);

	err = cio_websocket_write_message_first_chunk(websocket, cio_write_buffer_get_total_size(&fws->wbh), &fws->wbh, true, false, write_handler, fws);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing third frame of text message did not succeed!");
}

static void write_handler_second_fragment(struct cio_websocket *websocket, void *context, enum cio_error err)
{
	write_handler_fake.custom_fake = write_handler_third_fragment;

	struct fragmented_websocket *fws = (struct fragmented_websocket *)context;

	TEST_ASSERT_TRUE_MESSAGE(check_frame(CIO_WEBSOCKET_TEXT_FRAME, fws->first_fragment, sizeof(fws->first_fragment), false), "Written first fragment not correct");

	cio_write_buffer_head_init(&fws->wbh);
	cio_write_buffer_element_init(&fws->wb, fws->second_fragment, sizeof(fws->second_fragment));
	cio_write_buffer_queue_tail(&fws->wbh, &fws->wb);

	err = cio_websocket_write_message_first_chunk(websocket, cio_write_buffer_get_total_size(&fws->wbh), &fws->wbh, false, false, write_handler, fws);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing second frame of text message did not succeed!");
}

static void test_send_fragmented_text_frame(void)
{
	write_handler_fake.custom_fake = write_handler_second_fragment;

	struct fragmented_websocket fws;
	cio_write_buffer_head_init(&fws.wbh);

	memcpy(fws.first_fragment, "Hello", strlen("Hello"));
	memcpy(fws.second_fragment, "World", strlen("World"));
	memcpy(fws.third_fragment, "olleH", strlen("olleH"));
	cio_write_buffer_element_init(&fws.wb, fws.first_fragment, sizeof(fws.first_fragment));
	cio_write_buffer_queue_tail(&fws.wbh, &fws.wb);

	enum cio_error err = cio_websocket_server_init(&fws.ws, on_connect, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Init did not succeeded");

	fws.ws.ws_private.http_client = &http_client;

	err = cio_websocket_write_message_first_chunk(&fws.ws, cio_write_buffer_get_total_size(&fws.wbh), &fws.wbh, false, false, write_handler, &fws);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a text frame did not succeed!");
	TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_CONTINUATION_FRAME, fws.third_fragment, sizeof(fws.third_fragment), true), "Third frame fragment sent is incorrect text frame!");

	TEST_ASSERT_EQUAL_MESSAGE(3, write_handler_fake.call_count, "write handler was not called!");
}

static void test_send_text_frame_no_ws(void)
{
	char data[5];
	memset(data, 'a', sizeof(data));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_message_first_chunk(NULL, cio_write_buffer_get_total_size(&wbh), &wbh, true, false, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Writing a text frame with not webscoket did not fail!");
}

static void test_send_text_frame_no_handler(void)
{
	char data[5];
	memset(data, 'a', sizeof(data));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_message_first_chunk(ws, cio_write_buffer_get_total_size(&wbh), &wbh, true, false, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Writing a text frame with no handler did not fail!");
}

static void test_send_text_frame_twice(void)
{
	cio_buffered_stream_write_fake.custom_fake = bs_write_later;

	char data[5];
	memset(data, 'a', sizeof(data));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_websocket_write_message_first_chunk(ws, cio_write_buffer_get_total_size(&wbh), &wbh, true, false, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a text frame did not succeed!");

	err = cio_websocket_write_message_first_chunk(ws, cio_write_buffer_get_total_size(&wbh), &wbh, true, false, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_NOT_PERMITTED, err, "Writing a second text frame did not fail!");
}

static void test_send_multiple_jobs(void)
{
	char buffer[125] = {'a'};

	cio_buffered_stream_write_fake.custom_fake = bs_write_later;

	struct cio_write_buffer ping_wbh;
	cio_write_buffer_head_init(&ping_wbh);
	struct cio_write_buffer pong_wbh;
	cio_write_buffer_head_init(&pong_wbh);
	struct cio_write_buffer text_wbh;
	cio_write_buffer_head_init(&text_wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&text_wbh, &wb);

	enum cio_error err = cio_websocket_write_ping(ws, &ping_wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a ping frame did not succeed!");
	err = cio_websocket_write_pong(ws, &pong_wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a pong frame did not succeed!");
	err = cio_websocket_write_message_first_chunk(ws, cio_write_buffer_get_total_size(&text_wbh), &text_wbh, true, false, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a text frame did not succeed!");
	err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a close frame did not succeed!");

	// Simulate write call over the eventloop
	cio_buffered_stream_write_fake.custom_fake = bs_write_ok;
	bs_write_ok(write_later_bs, write_later_buf, write_later_handler, write_later_handler_context);

	TEST_ASSERT_EQUAL_MESSAGE(4, write_handler_fake.call_count, "Write handler was not called");

	TEST_ASSERT_TRUE_MESSAGE(check_frame(CIO_WEBSOCKET_PING_FRAME, NULL, 0, true), "Written ping frame not correct");
	TEST_ASSERT_TRUE_MESSAGE(check_frame(CIO_WEBSOCKET_PONG_FRAME, NULL, 0, true), "Written pong frame not correct");
	TEST_ASSERT_TRUE_MESSAGE(check_frame(CIO_WEBSOCKET_TEXT_FRAME, buffer, sizeof(buffer), true), "Written text frame not correct");
	TEST_ASSERT_TRUE_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "Written close frame not correct");
}

static void test_send_multiple_jobs_starting_with_close(void)
{
	char buffer[125] = {'a'};

	cio_buffered_stream_write_fake.custom_fake = bs_write_later;

	struct cio_write_buffer ping_wbh;
	cio_write_buffer_head_init(&ping_wbh);
	struct cio_write_buffer pong_wbh;
	cio_write_buffer_head_init(&pong_wbh);
	struct cio_write_buffer text_wbh;
	cio_write_buffer_head_init(&text_wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&text_wbh, &wb);

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a close frame did not succeed!");
	err = cio_websocket_write_ping(ws, &ping_wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a ping frame did not succeed!");
	err = cio_websocket_write_pong(ws, &pong_wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a pong frame did not succeed!");
	err = cio_websocket_write_message_first_chunk(ws, cio_write_buffer_get_total_size(&text_wbh), &text_wbh, true, false, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a text frame did not succeed!");

	// Simulate write call over the eventloop
	cio_buffered_stream_write_fake.custom_fake = bs_write_ok;
	bs_write_ok(write_later_bs, write_later_buf, write_later_handler, write_later_handler_context);

	TEST_ASSERT_EQUAL_MESSAGE(4, write_handler_fake.call_count, "Write handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_history[0], "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_history[0], "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_history[3], "err parameter of write_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_history[1], "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_history[1], "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_ABORTED, write_handler_fake.arg2_history[0], "err parameter of write_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_history[2], "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_history[2], "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_ABORTED, write_handler_fake.arg2_history[1], "err parameter of write_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, write_handler_fake.arg0_history[3], "websocket parameter of write_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(write_handler_fake.arg1_history[3], "context parameter of write_handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_ABORTED, write_handler_fake.arg2_history[2], "err parameter of write_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_TRUE_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "Written close frame not correct");
}

static void test_client_init(void)
{
	enum cio_error err = cio_websocket_client_init(ws, on_connect, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Wrong error code for successful client init");
}

static void test_client_init_without_ws(void)
{
	enum cio_error err = cio_websocket_client_init(NULL, on_connect, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no ws pointer provided");
}

static void test_client_init_without_on_connect(void)
{
	enum cio_error err = cio_websocket_client_init(ws, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no on_connect function provided");
}

static void test_server_init(void)
{
	enum cio_error err = cio_websocket_server_init(ws, on_connect, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Wrong error code for successful server init");
}

static void test_server_init_without_ws(void)
{
	enum cio_error err = cio_websocket_server_init(NULL, on_connect, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no ws pointer provided");
}

static void test_server_init_without_on_connect(void)
{
	enum cio_error err = cio_websocket_server_init(ws, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no on_connect function provided");
}

static void test_read_message_no_websocket(void)
{
	enum cio_error err = cio_websocket_read_message(NULL, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no websocket pointer is provided!");
}

static void test_read_message_no_handler(void)
{
	enum cio_error err = cio_websocket_read_message(ws, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no handler is provided!");
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_receive_unfragmented_frames);
	RUN_TEST(test_receive_unfragmented_frames_in_parts);
	RUN_TEST(test_receive_fragmented_frames);

	RUN_TEST(test_incoming_ping_pong_send_fails);
	RUN_TEST(test_close_close_response_fails);
	RUN_TEST(test_close_self_sendframe_fails);
	RUN_TEST(test_send_multiple_jobs_with_failures);

	RUN_TEST(test_immediate_read_error_for_get_header);
	RUN_TEST(test_immediate_read_error_for_get_first_length);
	RUN_TEST(test_immediate_read_error_for_get_mask);
	RUN_TEST(test_immediate_read_error_for_get_extended_length);
	RUN_TEST(test_immediate_read_error_for_get_payload);
	RUN_TEST(test_immediate_read_error_for_second_get_payload);

	RUN_TEST(test_read_error_in_get_header);
	RUN_TEST(test_read_error_in_get_mask);
	RUN_TEST(test_read_error_in_get_payload);
	RUN_TEST(test_read_no_utf8);

	RUN_TEST(test_receive_ping_frame);
	RUN_TEST(test_receive_ping_frame_no_payload);
	RUN_TEST(test_receive_ping_frame_no_callback);
	RUN_TEST(test_receive_ping_frame_payload_too_long);
	RUN_TEST(test_recieve_ping_frame_payload_too_long_no_error_callback);
	RUN_TEST(test_receive_ping_frame_restart_receive_fails_after_pong_written);

	RUN_TEST(test_receive_pong_frame);
	RUN_TEST(test_receive_pong_frame_no_callback);
	RUN_TEST(test_receive_pong_frame_restart_receive_fails);

	RUN_TEST(test_illegal_opcode);
	RUN_TEST(test_wrong_opcode_between_fragments);
	RUN_TEST(test_wrong_opcode_in_fragment);
	RUN_TEST(test_control_frame_within_fragment);
	RUN_TEST(test_binary_frame_within_text_fragments);

	RUN_TEST(test_rsv_bit_in_header);
	RUN_TEST(test_fragmented_control_frame);
	RUN_TEST(test_wrong_continuation_frame_without_correct_start_frame);
	RUN_TEST(test_wrong_fragment_start);
	RUN_TEST(test_no_mask_set_from_client);

	RUN_TEST(test_close_self_no_answer);

	RUN_TEST(test_close_in_get_header);
	RUN_TEST(test_close_in_get_length);
	RUN_TEST(test_close_in_get_mask);
	RUN_TEST(test_close_in_get_payload);

	RUN_TEST(test_close_self_no_reason);
	RUN_TEST(test_close_self_with_reason);
	RUN_TEST(test_close_self_no_ws);
	RUN_TEST(test_close_self_no_handler);
	RUN_TEST(test_close_self_twice);
	RUN_TEST(test_close_self_timer_init_fails);
	RUN_TEST(test_close_self_timer_expire_fails);
	RUN_TEST(test_close_self_without_read);
	RUN_TEST(test_close_self_without_close_hook);
	RUN_TEST(test_close_with_invalid_status_codes);
	RUN_TEST(test_close_with_valid_status_codes);
	RUN_TEST(test_close_reason_not_utf8);
	RUN_TEST(test_text_frame_utf8_no_complete_in_last_frame);

	RUN_TEST(test_close_short_status);

	RUN_TEST(test_send_pong_frame);
	RUN_TEST(test_send_pong_frame_no_ws);
	RUN_TEST(test_send_ping_frame_no_ws);
	RUN_TEST(test_send_pong_frame_no_handler);
	RUN_TEST(test_send_pong_frame_payload_too_large);
	RUN_TEST(test_send_pong_frame_twice);

	RUN_TEST(test_send_ping_frame);

	RUN_TEST(test_send_text_binary_frame);
	RUN_TEST(test_send_chunks);
	RUN_TEST(test_send_fragmented_text_frame);
	RUN_TEST(test_send_text_frame_no_ws);
	RUN_TEST(test_send_text_frame_no_handler);
	RUN_TEST(test_send_text_frame_twice);

	RUN_TEST(test_send_multiple_jobs);
	RUN_TEST(test_send_multiple_jobs_starting_with_close);

	RUN_TEST(test_client_init);
	RUN_TEST(test_client_init_without_ws);
	RUN_TEST(test_client_init_without_on_connect);

	RUN_TEST(test_server_init);
	RUN_TEST(test_server_init_without_ws);
	RUN_TEST(test_server_init_without_on_connect);

	RUN_TEST(test_read_message_no_websocket);
	RUN_TEST(test_read_message_no_handler);

	return UNITY_END();
}
