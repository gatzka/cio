/*
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

#define _DEFAULT_SOURCE
#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_random.h"
#include "cio_timer.h"
#include "cio_websocket.h"
#include "cio_websocket_masking.h"

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

static struct cio_websocket *ws;
static struct cio_buffered_stream buffered_stream;
static struct cio_read_buffer rb;

enum cio_ws_frame_type {
	CIO_WEBSOCKET_CONTINUATION_FRAME = 0x0,
	CIO_WEBSOCKET_TEXT_FRAME = 0x1,
	CIO_WEBSOCKET_BINARY_FRAME = 0x2,
	CIO_WEBSOCKET_CLOSE_FRAME = 0x8,
	CIO_WEBSOCKET_PING_FRAME = 0x9,
	CIO_WEBSOCKET_PONG_FRAME = 0x0a,
};

enum frame_direction {
	FROM_CLIENT,
	FROM_SERVER
};

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)

static enum cio_error timer_cancel(struct cio_timer *t);
FAKE_VALUE_FUNC(enum cio_error, timer_cancel, struct cio_timer *)

static void timer_close(struct cio_timer *t);
FAKE_VOID_FUNC(timer_close, struct cio_timer *)

static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, timer_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, timer_expires_from_now, struct cio_timer *, uint64_t, timer_handler, void *)

static enum cio_error read_exactly(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, read_exactly, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *)

static enum cio_error bs_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler, void *)

static void on_close(const struct cio_websocket *ws, enum cio_websocket_status_code status, const char *reason, size_t reason_length);
FAKE_VOID_FUNC(on_close, const struct cio_websocket *, enum cio_websocket_status_code, const char *, size_t)

static void on_textframe(struct cio_websocket *ws, uint8_t *data, size_t length, bool last_frame);
FAKE_VOID_FUNC(on_textframe, struct cio_websocket *, uint8_t *, size_t, bool)

static void on_binaryframe(struct cio_websocket *ws, uint8_t *data, size_t length, bool last_frame);
FAKE_VOID_FUNC(on_binaryframe, struct cio_websocket *, uint8_t *, size_t, bool)

static void on_error(const struct cio_websocket *ws, enum cio_websocket_status_code status, const char *reason);
FAKE_VOID_FUNC(on_error, const struct cio_websocket *, enum cio_websocket_status_code, const char *)

static void on_ping(struct cio_websocket *ws, const uint8_t *data, size_t length);
FAKE_VOID_FUNC(on_ping, struct cio_websocket *, const uint8_t *, size_t)

static void on_pong(struct cio_websocket *ws, uint8_t *data, size_t length);
FAKE_VOID_FUNC(on_pong, struct cio_websocket *, uint8_t *, size_t)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define WS_HEADER_FIN 0x80
#define WS_MASK_SET 0x80

void cio_random_get_bytes(void *bytes, size_t num_bytes)
{
	for (unsigned int i = 0; i < num_bytes; i++) {
		((uint8_t *)bytes)[i] = 0;
	}
}

struct ws_frame {
	enum cio_ws_frame_type frame_type;
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

		frame_buffer[buffer_pos++] |= frame.frame_type;

		if (frame.direction == FROM_CLIENT) {
			frame_buffer[buffer_pos] = WS_MASK_SET;
		} else {
			frame_buffer[buffer_pos] = 0x00;
		}

		if (frame.data_length <= 125) {
			frame_buffer[buffer_pos] |= (uint8_t)frame.data_length;
			buffer_pos++;
		} else if (frame.data_length < 65536) {
			uint16_t len = (uint16_t)frame.data_length;
			frame_buffer[buffer_pos] |= 126;
			buffer_pos++;
			len = htobe16(len);
			memcpy(&frame_buffer[buffer_pos], &len, sizeof(len));
			buffer_pos += sizeof(len);
		} else {
			frame_buffer[buffer_pos] |= 127;
			buffer_pos++;
			uint64_t len = (uint64_t)frame.data_length;
			len = htobe64(len);
			memcpy(&frame_buffer[buffer_pos], &len, sizeof(len));
			buffer_pos += sizeof(len);
		}

		uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
		if (frame.direction == FROM_CLIENT) {
			memcpy(&frame_buffer[buffer_pos], mask, sizeof(mask));
			buffer_pos += sizeof(mask);
		}

		if (frame.data_length > 0) {
			memcpy(&frame_buffer[buffer_pos], frame.data, frame.data_length);
			if (frame.direction == FROM_CLIENT) {
				cio_websocket_mask(&frame_buffer[buffer_pos], frame.data_length, mask);
			}

			buffer_pos += frame.data_length;
		}
	}

	frame_buffer_fill_pos = buffer_pos - 1;
}

static enum cio_error bs_read_exactly_from_buffer(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (frame_buffer_read_pos > frame_buffer_fill_pos) {
		handler(bs, handler_context, CIO_EOF, buffer);
	} else {
		memcpy(buffer->add_ptr, &frame_buffer[frame_buffer_read_pos], num);
		buffer->bytes_transferred = num;
		buffer->fetch_ptr = buffer->add_ptr + num;
		frame_buffer_read_pos += num;

		handler(bs, handler_context, CIO_SUCCESS, buffer);
	}

	return CIO_SUCCESS;
}

static enum cio_error bs_read_exactly_immediate_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;
	(void)handler;
	(void)handler_context;
	return CIO_ADDRESS_IN_USE;
}

static enum cio_error bs_read_exactly_peer_close(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)num;
	handler(bs, handler_context, CIO_EOF, buffer);
	return CIO_SUCCESS;
}

static enum cio_error bs_read_exactly_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)num;
	handler(bs, handler_context, CIO_OPERATION_NOT_PERMITTED, buffer);
	return CIO_SUCCESS;
}

static void on_textframe_save_data(struct cio_websocket *websocket, uint8_t *data, size_t length, bool last_frame)
{
	(void)last_frame;
	(void)websocket;
	memcpy(&read_back_buffer[read_back_buffer_pos], data, length);
	read_back_buffer_pos += length;
}

static void on_binaryframe_save_data(struct cio_websocket *websocket, uint8_t *data, size_t length, bool last_frame)
{
	(void)last_frame;
	(void)websocket;
	memcpy(&read_back_buffer[read_back_buffer_pos], data, length);
	read_back_buffer_pos += length;
}

static void on_error_save_data(const struct cio_websocket *websocket, enum cio_websocket_status_code status, const char *reason)
{
	(void)websocket;
	(void)status;
	strcpy((char *)&read_back_buffer[read_back_buffer_pos], reason);
	read_back_buffer_pos += strlen(reason);
}

static void on_ping_frame_save_data(struct cio_websocket *websocket, const uint8_t *data, size_t length)
{
	(void)websocket;
	memcpy(&read_back_buffer[read_back_buffer_pos], data, length);
	read_back_buffer_pos += length;
}

static void on_close_frame_save_data(const struct cio_websocket *websocket, enum cio_websocket_status_code status, const char *reason, size_t length)
{
	(void)websocket;
	uint16_t stat = status;
	stat = be16toh(stat);
	memcpy(&read_back_buffer[read_back_buffer_pos], &stat, sizeof(status));
	read_back_buffer_pos += sizeof(stat);
	memcpy(&read_back_buffer[read_back_buffer_pos], reason, length);
	read_back_buffer_pos += length;
}

static void websocket_free(struct cio_websocket *s)
{
	free(s);
}

static enum cio_error bs_write_ok(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	handler(bs, handler_context, buf, CIO_SUCCESS);
	return CIO_SUCCESS;
}

static enum cio_error bs_write_later(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)bs;
	(void)buf;
	(void)handler;
	(void)handler_context;
	return CIO_SUCCESS;
}

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook)
{
	(void)l;
	timer->cancel = timer_cancel;
	timer->close = timer_close;
	timer->close_hook = hook;
	timer->expires_from_now = timer_expires_from_now;
	return CIO_SUCCESS;
}

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(cio_timer_init);
	RESET_FAKE(timer_cancel);
	RESET_FAKE(timer_close);
	RESET_FAKE(timer_expires_from_now);
	RESET_FAKE(read_exactly);
	RESET_FAKE(bs_write);
	RESET_FAKE(on_close);
	RESET_FAKE(on_textframe);
	RESET_FAKE(on_binaryframe);
	RESET_FAKE(on_error);
	RESET_FAKE(on_ping);
	RESET_FAKE(on_pong);

	cio_read_buffer_init(&rb, read_buffer, sizeof(read_buffer));
	ws = malloc(sizeof(*ws));
	cio_websocket_init(ws, true, websocket_free);
	ws->rb = &rb;
	ws->bs = &buffered_stream;
	ws->on_close = on_close;
	ws->on_textframe = on_textframe;
	ws->on_binaryframe = on_binaryframe;
	ws->on_error = on_error;
	ws->on_ping = on_ping;
	ws->on_pong = on_pong;

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;

	buffered_stream.read_exactly = read_exactly;
	buffered_stream.write = bs_write;
	frame_buffer_read_pos = 0;
	frame_buffer_fill_pos = 0;
	read_back_buffer_pos = 0;
}

static void test_unfragmented_frames(void)
{
	uint32_t frame_sizes[] = {0, 1, 5, 125, 126, 65535, 65536};
	unsigned int frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(frame_types); j++) {
			unsigned int frame_type = frame_types[j];
			uint32_t frame_size = frame_sizes[i];
			char *data = malloc(frame_size);
			memset(data, 'a', frame_size);
			struct ws_frame frames[] = {
			    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
			    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
			};

			serialize_frames(frames, ARRAY_SIZE(frames));
			read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
			on_textframe_fake.custom_fake = on_textframe_save_data;
			on_binaryframe_fake.custom_fake = on_binaryframe_save_data;

			ws->internal_on_connect(ws);

			if (frame_type == CIO_WEBSOCKET_BINARY_FRAME) {
				TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
				TEST_ASSERT_EQUAL_MESSAGE(1, on_binaryframe_fake.call_count, "callback for binary frames was not called");
				TEST_ASSERT_EQUAL_MESSAGE(ws, on_binaryframe_fake.arg0_val, "ws parameter in binary frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_binaryframe_fake.arg2_val, "data length in binary frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(true, on_binaryframe_fake.arg3_val, "last_frame in binary frame callback not set");
			} else {
				TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
				TEST_ASSERT_EQUAL_MESSAGE(1, on_textframe_fake.call_count, "callback for text frames was not called");
				TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_val, "ws parameter in text frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_val, "data length in text frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(true, on_textframe_fake.arg3_val, "last_frame in text frame callback not set");
			}

			TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
			if (frame_size > 0) {
				TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, frame_size, "data in data/binaray frame callback not correct");
			}

			TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close was not called");

			if (data) {
				free(data);
			}

			setUp();
		}
	}

	free(ws);
}

static void test_fragmented_frames(void)
{
	uint32_t frame_sizes[] = {0, 1, 5, 125, 126, 65535, 65536};
	unsigned int frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(frame_types); j++) {
			unsigned int frame_type = frame_types[j];
			uint32_t frame_size = frame_sizes[i];
			char *first_data = malloc(frame_size);
			memset(first_data, 'a', frame_size);
			char *last_data = malloc(frame_size);
			memset(last_data, 'b', frame_size);
			struct ws_frame frames[] = {
			    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = first_data, .data_length = frame_size, .last_frame = false},
			    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = last_data, .data_length = frame_size, .last_frame = true, .rsv = false},
			    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
			};

			serialize_frames(frames, ARRAY_SIZE(frames));
			read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
			on_textframe_fake.custom_fake = on_textframe_save_data;
			on_binaryframe_fake.custom_fake = on_binaryframe_save_data;

			ws->internal_on_connect(ws);

			if (frame_type == CIO_WEBSOCKET_BINARY_FRAME) {
				TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
				TEST_ASSERT_EQUAL_MESSAGE(2, on_binaryframe_fake.call_count, "callback for binary frames was not called");
				TEST_ASSERT_EQUAL_MESSAGE(ws, on_binaryframe_fake.arg0_history[0], "ws parameter in first fragment of binary frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(ws, on_binaryframe_fake.arg0_history[1], "ws parameter in first fragment of binary frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_binaryframe_fake.arg2_history[0], "data length in first fragment of binary frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_binaryframe_fake.arg2_history[1], "data length in last fragment of binary frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(false, on_binaryframe_fake.arg3_history[0], "last_frame in binary frame callback set");
				TEST_ASSERT_EQUAL_MESSAGE(true, on_binaryframe_fake.arg3_history[1], "last_frame in binary frame callback not set");
			} else {
				TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
				TEST_ASSERT_EQUAL_MESSAGE(2, on_textframe_fake.call_count, "callback for text frames was not called");
				TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[0], "ws parameter in first fragment of text frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[1], "ws parameter in second fragment of text frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[0], "data length in first fragment of text frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[1], "data length in second fragment of text frame callback not correct");
				TEST_ASSERT_EQUAL_MESSAGE(false, on_textframe_fake.arg3_history[0], "last_frame in first fragment of text frame callback set");
				TEST_ASSERT_EQUAL_MESSAGE(true, on_textframe_fake.arg3_history[1], "last_frame in second fragment of text frame callback not set");
			}

			TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
			if (frame_size > 0) {
				TEST_ASSERT_EQUAL_MEMORY_MESSAGE(first_data, read_back_buffer, frame_size, "data in data/binaray frame callback not correct");
				TEST_ASSERT_EQUAL_MEMORY_MESSAGE(last_data, &read_back_buffer[frame_size], frame_size, "data in data/binaray frame callback not correct");
			}

			TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close was not called");

			if (first_data) {
				free(first_data);
			}

			if (last_data) {
				free(last_data);
			}

			setUp();
		}
	}

	free(ws);
}

static void test_incoming_ping_frame(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_ping_fake.custom_fake = on_ping_frame_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_ping_fake.arg0_val, "ws parameter in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_ping_fake.arg2_val, "data length in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "data in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was called");
}

static void test_ping_frame_no_callback(void)
{
	ws->on_ping = NULL;
	char data[] = "aaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_ping_fake.custom_fake = on_ping_frame_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was called");
}

static void test_pong_frame(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PONG_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_ping_fake.custom_fake = on_ping_frame_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_pong_fake.call_count, "callback for pong frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_pong_fake.arg0_val, "ws parameter in pong frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_pong_fake.arg2_val, "data length in ponng frame callback not correct");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "data in pong frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was called");
}

static void test_pong_frame_no_callback(void)
{
	ws->on_pong = NULL;
	char data[] = "aaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PONG_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_ping_fake.custom_fake = on_ping_frame_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was called");
}

static void test_close_frame_pong_not_written(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_later;
	on_ping_fake.custom_fake = on_ping_frame_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_ping_fake.arg0_val, "ws parameter in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_ping_fake.arg2_val, "data length in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "data in ping frame callback not correct");
}

static void test_ping_frame_no_payload(void)
{
	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_ping_fake.custom_fake = on_ping_frame_save_data;
	on_error_fake.custom_fake = on_error_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_ping_fake.arg0_val, "ws parameter in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.arg2_val, "data length in ping frame callback not correct");
}

static void test_ping_frame_payload_too_long(void)
{
	char data[126] = {'a'};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_ping_fake.custom_fake = on_ping_frame_save_data;
	on_error_fake.custom_fake = on_error_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in first fragment of error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, on_error_fake.arg1_val, "status parameter in error callback not correct");
	TEST_ASSERT_EQUAL_STRING_MESSAGE("payload of control frame too long", read_back_buffer, "reason in error callback not correct");
}

static void test_close_in_get_header(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_peer_close;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_read_error_in_get_header(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_error;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_read_error_in_get_first_length(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_close_in_get_first_length(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_peer_close};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_immediate_read_error_for_get_first_length(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_immediate_read_error_for_get_mask_or_payload(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_immediate_read_error_for_get_header(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_immediate_read_error_for_get_length16(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 3000;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_close_in_get_length16(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 30000;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_peer_close};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_read_error_in_get_length16(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 30000;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_close_in_get_length64(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 70000;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_peer_close};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_read_error_in_get_length64(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 70000;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_close_in_get_mask(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_peer_close};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_read_error_in_get_mask(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_immediate_read_error_for_get_payload(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_TOO_LARGE, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_close_in_get_payload(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_peer_close};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_read_error_in_get_payload(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_from_buffer,
	    bs_read_exactly_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_rsv_bit_in_header(void)
{
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = true},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_fragmented_control_frame(void)
{
	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = false, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
}

static void test_wrong_continuation_frame_without_correct_start_frame(void)
{
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");

	if (data) {
		free(data);
	}
}

static void test_three_fragments(void)
{
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(3, on_textframe_fake.call_count, "callback for text frames was not called correctly");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[0], "ws parameter in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[1], "ws parameter in second fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[2], "ws parameter in second fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[0], "data length in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[1], "data length in second fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[2], "data length in second fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(false, on_textframe_fake.arg3_history[0], "last_frame in first fragment of text frame callback set");
	TEST_ASSERT_EQUAL_MESSAGE(false, on_textframe_fake.arg3_history[1], "last_frame in second fragment of text frame callback set");
	TEST_ASSERT_EQUAL_MESSAGE(true, on_textframe_fake.arg3_history[2], "last_frame in second fragment of text frame callback not set");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was not called");

	if (data) {
		free(data);
	}
}

static void test_wrong_opcode_between_fragments(void)
{
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(2, on_textframe_fake.call_count, "callback for text frames was not called correctly");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[0], "ws parameter in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[1], "ws parameter in second fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[0], "data length in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[1], "data length in second fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(false, on_textframe_fake.arg3_history[0], "last_frame in first fragment of text frame callback set");
	TEST_ASSERT_EQUAL_MESSAGE(false, on_textframe_fake.arg3_history[1], "last_frame in second fragment of text frame callback set");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was not called");

	if (data) {
		free(data);
	}
}

static void test_wrong_opcode_in_fragment(void)
{
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(2, on_textframe_fake.call_count, "callback for text frames was not called correctly");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[0], "ws parameter in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[0], "data length in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(false, on_textframe_fake.arg3_history[0], "last_frame in first fragment of text frame callback set");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was not called");

	if (data) {
		free(data);
	}
}

static void test_ping_within_fragment(void)
{
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(3, on_textframe_fake.call_count, "callback for text frames was not called correctly");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[0], "ws parameter in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[0], "data length in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(false, on_textframe_fake.arg3_history[0], "last_frame in first fragment of text frame callback set");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was not called");

	if (data) {
		free(data);
	}
}

static void test_binary_frame_within_fragment(void)
{
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = false, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_BINARY_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(2, on_textframe_fake.call_count, "callback for text frames was not called correctly");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_textframe_fake.arg0_history[0], "ws parameter in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(frame_size, on_textframe_fake.arg2_history[0], "data length in first fragment of text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(false, on_textframe_fake.arg3_history[0], "last_frame in first fragment of text frame callback set");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was not called");

	if (data) {
		free(data);
	}
}

static void test_wrong_fragment_start(void)
{
	uint32_t frame_size = 5;
	char *data = malloc(frame_size);
	memset(data, 'a', frame_size);

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was not called correctly");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was not called");

	if (data) {
		free(data);
	}
}

static void test_illegal_opcode(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PONG_FRAME + 1, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_ping_fake.custom_fake = on_ping_frame_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
}

static void test_ping_frame_pong_not_written(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_later;
	on_ping_fake.custom_fake = on_ping_frame_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_ping_fake.call_count, "callback for ping frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_ping_fake.arg0_val, "ws parameter in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_ping_fake.arg2_val, "data length in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "data in ping frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
}

static void test_close_short_status(void)
{
	char data[] = {0xa};

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, on_error_fake.arg1_val, "error code parameter in error frame callback not correct");
}

static void test_close_with_message(void)
{

	uint8_t data[] = {0x3, 0xe8, 'G', 'o', 'o', 'd', ' ', 'B', 'y', 'e'};

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	on_close_fake.custom_fake = on_close_frame_save_data;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_close_fake.arg0_val, "ws parameter in close callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, sizeof(data), "echoed data in close frame not correct");
}

static void test_close_invalid_status(void)
{
	uint8_t data[] = {0x3, 0xec};

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, on_error_fake.arg1_val, "error code parameter in error frame callback not correct");
}

static void test_close_reason_not_utf8(void)
{
	uint8_t data[] = {0x3, 0xe8, 0xf8, 0x88, 0x80, 0x80, 0x80};

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, on_error_fake.arg1_val, "error code parameter in error frame callback not correct");
}

static void test_text_frame_not_utf8(void)
{
	uint8_t data[] = {0xf8, 0x88, 0x80, 0x80, 0x80};

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA, on_error_fake.arg1_val, "error code parameter in error frame callback not correct");
}

static void test_text_frame_no_callback(void)
{
	uint8_t data[] = "Hello";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	ws->on_textframe = NULL;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_UNSUPPORTED, on_error_fake.arg1_val, "error code parameter in error frame callback not correct");
}

static void test_text_frame_fragmented_not_utf8(void)
{
	uint8_t data0[] = "Hello";
	uint8_t data1[] = {'\xe4','\xbd','\xa0','\xe5','\xa5'};

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data0, .data_length = sizeof(data0), .last_frame = false, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data1, .data_length = sizeof(data1), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(1, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA, on_error_fake.arg1_val, "error code parameter in error frame callback not correct");
}

static void test_binary_frame_no_callback(void)
{
	uint8_t data[] = "Hello";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_BINARY_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	ws->on_binaryframe = NULL;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_UNSUPPORTED, on_error_fake.arg1_val, "error code parameter in error frame callback not correct");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_unfragmented_frames);
	RUN_TEST(test_fragmented_frames);
	RUN_TEST(test_incoming_ping_frame);
	RUN_TEST(test_ping_frame_no_callback);
	RUN_TEST(test_pong_frame);
	RUN_TEST(test_pong_frame_no_callback);
	RUN_TEST(test_ping_frame_no_payload);
	RUN_TEST(test_ping_frame_payload_too_long);
	RUN_TEST(test_close_frame_pong_not_written);

	RUN_TEST(test_immediate_read_error_for_get_header);

	RUN_TEST(test_close_in_get_header);
	RUN_TEST(test_read_error_in_get_header);
	RUN_TEST(test_immediate_read_error_for_get_first_length);

	RUN_TEST(test_close_in_get_first_length);
	RUN_TEST(test_read_error_in_get_first_length);
	RUN_TEST(test_immediate_read_error_for_get_mask_or_payload);

	RUN_TEST(test_immediate_read_error_for_get_length16);
	RUN_TEST(test_close_in_get_length16);
	RUN_TEST(test_read_error_in_get_length16);

	RUN_TEST(test_close_in_get_length64);
	RUN_TEST(test_read_error_in_get_length64);

	RUN_TEST(test_close_in_get_mask);
	RUN_TEST(test_read_error_in_get_mask);
	RUN_TEST(test_immediate_read_error_for_get_payload);

	RUN_TEST(test_close_in_get_payload);
	RUN_TEST(test_read_error_in_get_payload);

	RUN_TEST(test_rsv_bit_in_header);
	RUN_TEST(test_fragmented_control_frame);
	RUN_TEST(test_wrong_continuation_frame_without_correct_start_frame);
	RUN_TEST(test_three_fragments);
	RUN_TEST(test_wrong_opcode_between_fragments);
	RUN_TEST(test_wrong_opcode_in_fragment);
	RUN_TEST(test_ping_within_fragment);
	RUN_TEST(test_binary_frame_within_fragment);
	RUN_TEST(test_wrong_fragment_start);
	RUN_TEST(test_illegal_opcode);

	RUN_TEST(test_ping_frame_pong_not_written);

	RUN_TEST(test_close_short_status);
	RUN_TEST(test_close_with_message);
	RUN_TEST(test_close_invalid_status);

	RUN_TEST(test_close_reason_not_utf8);
	RUN_TEST(test_text_frame_not_utf8);
	RUN_TEST(test_text_frame_no_callback);
	RUN_TEST(test_text_frame_fragmented_not_utf8);

	RUN_TEST(test_binary_frame_no_callback);

	return UNITY_END();
}
