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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cio_buffered_stream.h"
#include "cio_endian.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_random.h"
#include "cio_timer.h"
#include "cio_websocket.h"
#include "cio_websocket_masking.h"

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)

static struct cio_websocket *ws;
static struct cio_http_client http_client;

enum frame_direction {
	FROM_CLIENT,
	FROM_SERVER
};

static void read_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err, size_t frame_length, uint8_t *data, size_t length, bool last_chunk, bool last_frame, bool is_binary);
FAKE_VOID_FUNC(read_handler, struct cio_websocket *, void *, enum cio_error, size_t, uint8_t *, size_t, bool, bool, bool)

static enum cio_error timer_cancel(struct cio_timer *t);
FAKE_VALUE_FUNC(enum cio_error, timer_cancel, struct cio_timer *)

static void timer_close(struct cio_timer *t);
FAKE_VOID_FUNC(timer_close, struct cio_timer *)

static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, timer_expires_from_now, struct cio_timer *, uint64_t, cio_timer_handler, void *)

static enum cio_error read_at_least(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, read_at_least, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *)
typedef enum cio_error (*read_at_least_fake_fun)(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *);

static enum cio_error bs_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler, void *)

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

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

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
static uint8_t read_buffer[140];
static uint8_t read_back_buffer[140000];
static size_t read_back_buffer_pos = 0;
static uint8_t write_buffer[140000];
static size_t write_buffer_pos = 0;
static size_t write_buffer_parse_pos = 0;

static struct cio_buffered_stream *write_later_bs;
static struct cio_write_buffer *write_later_buf;
static cio_buffered_stream_write_handler write_later_handler;
static void *write_later_handler_context;

static enum cio_error timer_expires_from_now_save(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context)
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

static enum cio_error bs_read_at_least_from_buffer(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
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

static enum cio_error bs_read_at_least_block(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;
	(void)handler;
	(void)handler_context;
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_peer_close(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)num;
	handler(bs, handler_context, CIO_EOF, buffer, 0);
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)num;
	handler(bs, handler_context, CIO_OPERATION_NOT_PERMITTED, buffer, 0);
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_immediate_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;
	(void)handler;
	(void)handler_context;

	return CIO_ADDRESS_IN_USE;
}

static void websocket_free(struct cio_websocket *s)
{
	free(s);
}

static enum cio_error bs_write_ok(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
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

static enum cio_error bs_write_error(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)bs;
	(void)buf;
	(void)handler;
	(void)handler_context;
	return CIO_MESSAGE_TOO_LONG;
}

static enum cio_error bs_write_later(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	write_later_bs = bs;
	write_later_buf = buf;
	write_later_handler = handler;
	write_later_handler_context = handler_context;
	return CIO_SUCCESS;
}

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook)
{
	(void)l;
	timer->handler = NULL;
	timer->cancel = timer_cancel;
	timer->close = timer_close;
	timer->close_hook = hook;
	timer->expires_from_now = timer_expires_from_now;
	return CIO_SUCCESS;
}

static enum cio_error cio_timer_init_fails(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook)
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

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(read_handler);

	RESET_FAKE(cio_timer_init);
	RESET_FAKE(timer_cancel);
	RESET_FAKE(timer_close);
	RESET_FAKE(timer_expires_from_now);
	RESET_FAKE(read_at_least);
	RESET_FAKE(bs_write);
	RESET_FAKE(on_connect);
	RESET_FAKE(on_control);
	RESET_FAKE(on_error);

	RESET_FAKE(close_handler);
	RESET_FAKE(write_handler);

	cio_read_buffer_init(&http_client.rb, read_buffer, sizeof(read_buffer));
	ws = malloc(sizeof(*ws));
	cio_websocket_init(ws, true, on_connect, NULL);
	ws->ws_private.http_client = &http_client;
	ws->on_control = on_control;
	cio_websocket_set_on_error_cb(ws, on_error);

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
	timer_expires_from_now_fake.custom_fake = timer_expires_from_now_save;
	timer_close_fake.custom_fake = timer_close_cancel;

	read_handler_fake.custom_fake = read_handler_save_data;
	read_at_least_fake.custom_fake = bs_read_at_least_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_control_fake.custom_fake = on_control_save_data;
	on_error_fake.custom_fake = on_error_save_data;

	http_client.bs.read_at_least = read_at_least;
	http_client.bs.write = bs_write;
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

static void test_incoming_ping_pong_send_fails(void)
{
	struct cio_websocket *my_ws = malloc(sizeof(*my_ws));
	enum cio_error err = cio_websocket_init(my_ws, true, on_connect, websocket_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not init websocket");
	my_ws->ws_private.http_client = &http_client;
	cio_websocket_set_on_error_cb(my_ws, on_error);
	my_ws->on_control = on_control;

	char data[] = "aaaa";

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	bs_write_fake.custom_fake = bs_write_error;

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
	uint8_t data[] = {0x3, 0xe8, 'G', 'o', 'o', 'd', ' ', 'B', 'y', 'e'};

	struct ws_frame frames[] = {
	    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
	};

	bs_write_fake.custom_fake = bs_write_error;

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
	bs_write_fake.custom_fake = bs_write_error;

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Close not failed correctly");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of error handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, on_error_fake.arg1_val, "error code in error handler not correct");
}

static void test_send_multiple_jobs_with_failures(void)
{
	char buffer[125] = {'a'};

	typedef enum cio_error (*write_func)(struct cio_buffered_stream * bs, struct cio_write_buffer * buffer, cio_buffered_stream_write_handler handler, void *handler_context);
	write_func write_funcs[3] = {bs_write_later, bs_write_error, bs_write_ok};

	bs_write_fake.custom_fake_seq = write_funcs;
	bs_write_fake.custom_fake_seq_len = ARRAY_SIZE(write_funcs);

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
	bs_write_fake.custom_fake = bs_write_error;
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
	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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
	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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
	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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
		enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_immediate_error};

		SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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
	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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
		enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_from_buffer,
		    bs_read_at_least_immediate_error};

		SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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
	read_at_least_fake.custom_fake = bs_read_at_least_error;

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

	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_error};

	SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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

	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_error};

	SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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

static void test_close_self_no_answer(void)
{
	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	read_at_least_fake.custom_fake = bs_read_at_least_block;

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
	read_at_least_fake.custom_fake = bs_read_at_least_peer_close;

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
			SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, 2);
		} else {
			read_at_least_fakes = malloc(3 * sizeof(read_at_least_fake_fun));
			read_at_least_fakes[0] = bs_read_at_least_from_buffer;
			read_at_least_fakes[1] = bs_read_at_least_from_buffer;
			read_at_least_fakes[2] = bs_read_at_least_peer_close;
			SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, 3);
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

	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_peer_close};

	SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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

	enum cio_error (*read_at_least_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_from_buffer,
	    bs_read_at_least_peer_close};

	SET_CUSTOM_FAKE_SEQ(read_at_least, read_at_least_fakes, ARRAY_SIZE(read_at_least_fakes));

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
	uint8_t data[] = {0x3, 0xe8};

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
	uint8_t data[] = {0x3, 0xe8};

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
	bs_write_fake.custom_fake = bs_write_later;

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
	TEST_ASSERT_EQUAL_MESSAGE(0, timer_cancel_fake.call_count, "Timer cancel was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, timer_close_fake.call_count, "Timer close was called");
}

static void test_close_self_timer_expire_fails(void)
{
	timer_expires_from_now_fake.custom_fake = NULL;
	timer_expires_from_now_fake.return_val = CIO_ADDRESS_IN_USE;

	enum cio_error err = cio_websocket_close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, "Going away", close_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, err, "Close not failed correctly");
	TEST_ASSERT_EQUAL_MESSAGE(0, timer_cancel_fake.call_count, "Timer cancel was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, timer_close_fake.call_count, "Timer close was called");
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
	enum cio_error err = cio_websocket_init(&my_ws, true, on_connect, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Init did not succeeded");

	my_ws.ws_private.http_client = &http_client;

	uint8_t data[] = {0x3, 0xe8};

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

	bs_write_fake.custom_fake = bs_write_later;

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

	enum cio_error err = cio_websocket_init(&fws.ws, true, on_connect, NULL);
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
	bs_write_fake.custom_fake = bs_write_later;

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

	bs_write_fake.custom_fake = bs_write_later;

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
	bs_write_fake.custom_fake = bs_write_ok;
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

	bs_write_fake.custom_fake = bs_write_later;

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
	bs_write_fake.custom_fake = bs_write_ok;
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

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_TRUE_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "Written close frame not correct");
}

int main(void)
{
	UNITY_BEGIN();

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

	return UNITY_END();
}
