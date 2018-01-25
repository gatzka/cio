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

#define _BSD_SOURCE
#include <endian.h>
#include <stdint.h>
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

static struct cio_websocket ws;
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

static enum cio_error read_exactly(struct cio_buffered_stream *bs, struct cio_read_buffer* buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, read_exactly, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *)

static enum cio_error bs_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler, void *)

static void on_close(const struct cio_websocket *ws, enum cio_websocket_status_code status, const char *reason, size_t reason_length);
FAKE_VOID_FUNC(on_close, const struct cio_websocket *, enum cio_websocket_status_code, const char *, size_t )

static void on_textframe(struct cio_websocket *ws, char *data, size_t length, bool last_frame);
FAKE_VOID_FUNC(on_textframe, struct cio_websocket *, char *, size_t, bool)

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
};

static uint8_t frame_buffer[70000];
static uint8_t read_buffer[70000];
static uint8_t read_back_buffer[70000];
static size_t frame_buffer_read_pos = 0;

static void serialize_frames(struct ws_frame frames[], size_t num_frames)
{
	uint8_t buffer_pos = 0;
	for (size_t i = 0; i < num_frames; i++) {
		struct ws_frame frame = frames[i];
		frame_buffer[buffer_pos++] = WS_HEADER_FIN | frame.frame_type;

		if (frame.direction == FROM_CLIENT) {
			frame_buffer[buffer_pos] = WS_MASK_SET;
		} else {
			frame_buffer[buffer_pos] = 0x00;
		}

		if (frame.data_length <= 125) {
			frame_buffer[buffer_pos] |= (uint8_t)frame.data_length;
			buffer_pos++;
		} else if (frame.data_length <= 65536) {
			frame_buffer[buffer_pos] |= 126;
			buffer_pos++;
			uint16_t len = (uint16_t)frame.data_length;
			len = htobe16(len);
			memcpy(&frame_buffer[buffer_pos], &len, sizeof(len));
			buffer_pos += sizeof(len);
		} else {
			frame_buffer[buffer_pos] |= 127;
			buffer_pos++;
			uint32_t len = (uint32_t)frame.data_length;
			len = htobe32(len);
			memcpy(&frame_buffer[buffer_pos], &len, sizeof(len));
			buffer_pos += sizeof(len);
		}

		if (frame.data_length > 0) {
			uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
			if (frame.direction == FROM_CLIENT) {
				memcpy(&frame_buffer[buffer_pos], mask, sizeof(mask));
				buffer_pos += sizeof(mask);
			}

			memcpy(&frame_buffer[buffer_pos], frame.data, frame.data_length);
			if (frame.direction == FROM_CLIENT) {
				cio_websocket_mask(&frame_buffer[buffer_pos], frame.data_length, mask);
			}

			buffer_pos += frame.data_length;
		}
	}
}

static enum cio_error bs_read_exactly_from_buffer(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	memcpy(buffer->add_ptr, &frame_buffer[frame_buffer_read_pos], num);
	buffer->bytes_transferred = num;
	buffer->fetch_ptr = buffer->add_ptr + num;
	frame_buffer_read_pos += num;

	handler(bs, handler_context, CIO_SUCCESS, buffer);
	return CIO_SUCCESS;
}

static void on_textframe_save_data(struct cio_websocket *websocket, char *data, size_t length, bool last_frame)
{
	(void)last_frame;
	(void)websocket;
	memcpy(read_back_buffer, data, length);
}

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(cio_timer_init);
	RESET_FAKE(read_exactly);
	RESET_FAKE(bs_write);
	RESET_FAKE(on_close);
	RESET_FAKE(on_textframe);
	RESET_FAKE(on_binaryframe);
	RESET_FAKE(on_error);
	RESET_FAKE(on_ping);
	RESET_FAKE(on_pong);

	cio_read_buffer_init(&rb, read_buffer, sizeof(read_buffer));
	cio_websocket_init(&ws, true, NULL);
	ws.rb = &rb;
	ws.bs = &buffered_stream;
	ws.on_close = on_close;
	ws.on_textframe = on_textframe;
	ws.on_binaryframe = on_binaryframe;
	ws.on_error = on_error;
	ws.on_ping = on_ping;
	ws.on_pong = on_pong;

	buffered_stream.read_exactly = read_exactly;
	buffered_stream.write = bs_write;
	frame_buffer_read_pos = 0;
}

static void test_small_text_frame(void)
{
	const char *data = "Hello";
	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = strlen(data)},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	on_textframe_fake.custom_fake = on_textframe_save_data;

	ws.receive_frames(&ws);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_textframe_fake.call_count, "callback for text frames was not called");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, read_back_buffer, strlen(data), "data in text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(&ws, on_textframe_fake.arg0_val, "ws parameter in text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(strlen(data), on_textframe_fake.arg2_val, "data length in text frame callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(true, on_textframe_fake.arg3_val, "last_frame in text frame callback not set");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close was not called");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_small_text_frame);
	return UNITY_END();
}
