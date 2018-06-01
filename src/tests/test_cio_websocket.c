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

void cio_random_get_bytes(void *bytes, size_t num_bytes)
{
	for (unsigned int i = 0; i < num_bytes; i++) {
		((uint8_t *)bytes)[i] = 0;
	}
}

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)

static struct cio_websocket *ws;
static struct cio_buffered_stream buffered_stream;
static struct cio_read_buffer rb;

enum frame_direction {
	FROM_CLIENT,
	FROM_SERVER
};

static void read_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err, uint8_t *data, size_t length, bool last_frame, bool is_binary);
FAKE_VOID_FUNC(read_handler, struct cio_websocket *, void *, enum cio_error, uint8_t *, size_t, bool, bool)



static enum cio_error timer_cancel(struct cio_timer *t);
FAKE_VALUE_FUNC(enum cio_error, timer_cancel, struct cio_timer *)

static void timer_close(struct cio_timer *t);
FAKE_VOID_FUNC(timer_close, struct cio_timer *)

static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, timer_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, timer_expires_from_now, struct cio_timer *, uint64_t, timer_handler, void *)

static enum cio_error read_exactly(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, read_exactly, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *)
typedef enum cio_error (*read_exactly_fake_fun)(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *);

static enum cio_error bs_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler, void *)

static void on_connect(struct cio_websocket *s);
FAKE_VOID_FUNC(on_connect, struct cio_websocket *)

static void on_control(const struct cio_websocket *ws, enum cio_websocket_frame_type type, const uint8_t *data, size_t length);
FAKE_VOID_FUNC(on_control, const struct cio_websocket *, enum cio_websocket_frame_type, const uint8_t *, size_t)

static void on_error(const struct cio_websocket *ws, enum cio_websocket_status_code status, const char *reason);
FAKE_VOID_FUNC(on_error, const struct cio_websocket *, enum cio_websocket_status_code, const char *)

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
static uint8_t read_buffer[140000];
static uint8_t read_back_buffer[140000];
static size_t read_back_buffer_pos = 0;
static uint8_t write_buffer[140000];
static size_t write_buffer_pos = 0;
static size_t write_buffer_parse_pos = 0;

#if 0
static bool check_frame(enum cio_websocket_frame_type opcode, const char *payload, size_t payload_length, bool is_last_frame)
{
	(void)payload;

	uint header = write_buffer[write_buffer_parse_pos++];
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
	first_length = first_length & ~WS_MASK_SET;

	uint64_t length;
	if (first_length == 126) {
		uint16_t len;
		memcpy(&len, &write_buffer[write_buffer_parse_pos], sizeof(len));
		write_buffer_parse_pos += sizeof(len);
		len = be16toh(len);
		length = len;
	} else if (first_length == 127) {
		uint64_t len;
		memcpy(&len, &write_buffer[write_buffer_parse_pos], sizeof(len));
		write_buffer_parse_pos += sizeof(len);
		len = be64toh(len);
		length = len;
	} else {
		length = first_length;
	}

	if (length != payload_length) {
		return false;
	}

	if (is_masked) {
		// TODO unmask_payload
	}

	if (memcmp(&write_buffer[write_buffer_parse_pos], payload, length) != 0) {
		return false;
	}

	write_buffer_parse_pos += payload_length;

	return true;
}

#endif
static bool is_close_frame(uint16_t status_code, bool status_code_required)
{
	if ((write_buffer[write_buffer_parse_pos] & WS_HEADER_FIN) != WS_HEADER_FIN) {
		return false;
	}

	if ((write_buffer[write_buffer_parse_pos++] & WS_CLOSE_FRAME) == 0) {
		return false;
	}

	uint8_t first_length = write_buffer[write_buffer_parse_pos++];
	first_length = first_length & ~WS_MASK_SET;
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
		uint16_t sc;
		memcpy(&sc, &write_buffer[write_buffer_parse_pos], sizeof(sc));
		sc = cio_be16toh(sc);
		write_buffer_parse_pos += sizeof(sc);
		first_length -= sizeof(sc);
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

static enum cio_error bs_read_exactly_immediate_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;

	handler(bs, handler_context, CIO_ADDRESS_IN_USE, buffer);
	return CIO_SUCCESS;
}
#if 0
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
	memcpy(&read_back_buffer[read_back_buffer_pos], &stat, sizeof(stat));
	read_back_buffer_pos += sizeof(stat);
	memcpy(&read_back_buffer[read_back_buffer_pos], reason, length);
	read_back_buffer_pos += length;
}

#endif

static void websocket_free(struct cio_websocket *s)
{
	free(s);
}
static enum cio_error bs_write_ok(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	size_t len = buf->data.q_len;
	for (unsigned int i = 0; i < len; i++) {
		buf = buf->next;
		memcpy(&write_buffer[write_buffer_pos], buf->data.element.data, buf->data.element.length);
		write_buffer_pos += buf->data.element.length;
	}

	handler(bs, handler_context, CIO_SUCCESS);
	return CIO_SUCCESS;
}

#if 0
static enum cio_error bs_write_later(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)bs;
	(void)buf;
	(void)handler;
	(void)handler_context;
	return CIO_SUCCESS;
}
#endif
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

	RESET_FAKE(read_handler);

	RESET_FAKE(cio_timer_init);
	RESET_FAKE(timer_cancel);
	RESET_FAKE(timer_close);
	RESET_FAKE(timer_expires_from_now);
	RESET_FAKE(read_exactly);
	RESET_FAKE(bs_write);
	RESET_FAKE(on_connect);
	RESET_FAKE(on_control);
	RESET_FAKE(on_error);
	RESET_FAKE(write_handler);

	cio_read_buffer_init(&rb, read_buffer, sizeof(read_buffer));
	ws = malloc(sizeof(*ws));
	cio_websocket_init(ws, true, websocket_free, on_connect);
	ws->rb = &rb;
	ws->bs = &buffered_stream;
	ws->on_control = on_control;
	ws->on_error = on_error;

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;

	buffered_stream.read_exactly = read_exactly;
	buffered_stream.write = bs_write;
	frame_buffer_read_pos = 0;
	frame_buffer_fill_pos = 0;

	memset(read_buffer, 0x00, sizeof(read_buffer));
	memset(read_back_buffer, 0x00, sizeof(read_back_buffer));
	read_back_buffer_pos = 0;
	write_buffer_pos = 0;
	write_buffer_parse_pos = 0;
	memset(write_buffer, 0x00, sizeof(write_buffer));
}

void tearDown(void)
{
	free(ws);
}

static void read_handler_save_data(struct cio_websocket *websocket, void *handler_context, enum cio_error err, uint8_t *data, size_t length, bool last_frame, bool is_binary)
{
	(void)err;
	(void)last_frame;
	(void)is_binary;

	if (length > 0) {
		memcpy(&read_back_buffer[read_back_buffer_pos], data, length);
		read_back_buffer_pos += length;
	}

	websocket->read_message(websocket, read_handler, handler_context);
}

static void on_control_save_data(const struct cio_websocket *websocket, enum cio_websocket_frame_type type,  const uint8_t *data, size_t length)
{
	(void)websocket;
	(void)type;
	if (length > 0) {
		memcpy(&read_back_buffer[read_back_buffer_pos], data, length);
		read_back_buffer_pos += length;
	}
}

static void on_error_save_data(const struct cio_websocket *websocket, enum cio_websocket_status_code status, const char *reason)
{
	(void)websocket;
	(void)status;
	strcpy((char *)&read_back_buffer[read_back_buffer_pos], reason);
	read_back_buffer_pos += strlen(reason);
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
			read_handler_fake.custom_fake = read_handler_save_data;

			ws->read_message(ws, read_handler, NULL);

			TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
			TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
			TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");
			TEST_ASSERT_EQUAL_MESSAGE(frame_size, read_handler_fake.arg4_val, "length parameter of read_handler not equal to frame_size");
			TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg5_val, "last_frame parameter of read_handler not true");

			if (frame_type == CIO_WEBSOCKET_BINARY_FRAME) {
				TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg6_val, "is_binary parameter of read_handler not true");
			} else {
				TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg6_val, "is_binary parameter of read_handler not false");
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
			read_handler_fake.custom_fake = read_handler_save_data;

			ws->read_message(ws, read_handler, NULL);

			TEST_ASSERT_EQUAL_MESSAGE(2, read_handler_fake.call_count, "read_handler was not called");
			TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_history[0], "websocket parameter of read_handler for first fragment not correct");
			TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_history[1], "websocket parameter of read_handler for last fragment not correct");
			TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[0], "context parameter of read handler for first fragment not NULL");
			TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[1], "context parameter of read handler for last fragment not NULL");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[0], "error parameter of read_handler for first fragment not CIO_SUCCESS");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[1], "error parameter of read_handler for last fragment not CIO_SUCCESS");
			TEST_ASSERT_EQUAL_MESSAGE(frame_size, read_handler_fake.arg4_history[0], "length parameter of read_handler for first fragment not equal to frame_size");
			TEST_ASSERT_EQUAL_MESSAGE(frame_size, read_handler_fake.arg4_history[1], "length parameter of read_handler for last fragment not equal to frame_size");
			TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg5_history[0], "last_frame parameter of read_handler for first fragment not false");
			TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg5_history[1], "last_frame parameter of read_handler for last fragment not true");

			if (frame_type == CIO_WEBSOCKET_BINARY_FRAME) {
				TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg6_history[0], "is_binary parameter of read_handler for first fragment not true");
				TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg6_history[1], "is_binary parameter of read_handler for last fragment not true");
			} else {
				TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg6_history[0], "is_binary parameter of read_handler for first fragment not false");
				TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg6_history[1], "is_binary parameter of read_handler for last fragment not false");
			}

			TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
			if (frame_size > 0) {
				TEST_ASSERT_EQUAL_MEMORY_MESSAGE(first_data, read_back_buffer, frame_size, "data in data/binaray frame callback not correct");
				TEST_ASSERT_EQUAL_MEMORY_MESSAGE(last_data, &read_back_buffer[frame_size], frame_size, "data in data/binaray frame callback not correct");
			}

			TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was not called for last close frame");
			TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_val, "websocket parameter of control callback is NULL");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "websocket parameter of control callback is NULL");
			TEST_ASSERT_NULL_MESSAGE(on_control_fake.arg2_val, "data parameter of control callback is not correct");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.arg3_val, "data length parameter of control callback is not correct");

			if (first_data) {
				free(first_data);
			}

			if (last_data) {
				free(last_data);
			}

			free(ws);
			setUp();
		}
	}
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
	on_control_fake.custom_fake = on_control_save_data;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

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

static void test_ping_frame_no_callback(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;

	ws->on_control = NULL;
	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
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
	on_control_fake.custom_fake = on_control_save_data;
	on_error_fake.custom_fake = on_error_save_data;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

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
	on_control_fake.custom_fake = on_control_save_data;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

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

static void test_pong_frame_no_callback(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PONG_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;

	ws->on_control = NULL;
	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
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
	on_error_fake.custom_fake = on_error_save_data;
	on_control_fake.custom_fake = on_control_save_data;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in first fragment of error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, on_error_fake.arg1_val, "status parameter in error callback not correct");
	TEST_ASSERT_EQUAL_STRING_MESSAGE("payload of control frame too long", read_back_buffer, "reason in error callback not correct");
	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, true), "written frame is not a close frame!");
}

static void test_ping_frame_payload_too_long_no_error_callback(void)
{
	char data[126] = {'a'};

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PING_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_control_fake.custom_fake = on_control_save_data;

	ws->on_error = NULL;
	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR, true), "written frame is not a close frame!");
}

static void test_close_in_get_header(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char data[frame_size];
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
		{.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_peer_close;
	bs_write_fake.custom_fake = bs_write_ok;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");
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
	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
		bs_read_exactly_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));
	bs_write_fake.custom_fake = bs_write_ok;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_read_error_in_get_header(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char data[frame_size];
	memset(data, 'a', frame_size);
	struct ws_frame frames[] = {
		{.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_error;
	bs_write_fake.custom_fake = bs_write_ok;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_immediate_read_error_for_get_first_length(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	uint32_t frame_size = 5;
	char data[frame_size];
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
	bs_write_fake.custom_fake = bs_write_ok;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_immediate_read_error_for_get_mask_or_payload(void)
{
	unsigned int frame_type = CIO_WEBSOCKET_TEXT_FRAME;
	char data[5];
	memset(data, 'a', sizeof(data));
	struct ws_frame frames[] = {
		{.frame_type = frame_type, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
		bs_read_exactly_from_buffer,
		bs_read_exactly_from_buffer,
		bs_read_exactly_immediate_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));
	bs_write_fake.custom_fake = bs_write_ok;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
}

static void test_immediate_read_error_for_get_length(void)
{
	uint32_t frame_sizes[] = {5, 30000, 70000};

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
		enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
			bs_read_exactly_from_buffer,
			bs_read_exactly_from_buffer,
			bs_read_exactly_immediate_error};

		SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));
		bs_write_fake.custom_fake = bs_write_ok;

		ws->read_message(ws, read_handler, NULL);

		TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

		TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
		TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");

		if (data) {
			free(data);
		}

		free(ws);
		setUp();
	}
}

static void test_close_in_get_length(void)
{
	uint32_t frame_sizes[] = {5};
	//uint32_t frame_sizes[] = {5, 30000, 70000};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		uint32_t frame_size = frame_sizes[i];
		char *data = malloc(frame_size);
		memset(data, 'a', frame_size);
		struct ws_frame frames[] = {
			{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = frame_size, .last_frame = true, .rsv = false},
			{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
		};

		serialize_frames(frames, ARRAY_SIZE(frames));

		read_exactly_fake_fun *read_exactly_fakes;
		if (frame_size <= 125) {
			read_exactly_fakes = malloc(2 * sizeof(read_exactly_fake_fun));
			read_exactly_fakes[0] = bs_read_exactly_from_buffer;
			read_exactly_fakes[1] = bs_read_exactly_peer_close;
			SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, 2);
		} else {
			read_exactly_fakes = malloc(2 * sizeof(read_exactly_fake_fun));
			read_exactly_fakes[0] = bs_read_exactly_from_buffer;
			read_exactly_fakes[1] = bs_read_exactly_from_buffer;
			read_exactly_fakes[2] = bs_read_exactly_peer_close;
			SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, 3);
		}

		bs_write_fake.custom_fake = bs_write_ok;

		ws->read_message(ws, read_handler, NULL);

		TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

		TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
		TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");

		if (data) {
			free(data);
		}

		free(read_exactly_fakes);

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

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
		bs_read_exactly_from_buffer,
		bs_read_exactly_from_buffer,
		bs_read_exactly_peer_close};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));
	bs_write_fake.custom_fake = bs_write_ok;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_NORMAL, on_error_fake.arg1_val, "error callback called with wrong status code");
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

	enum cio_error (*read_exactly_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *) = {
		bs_read_exactly_from_buffer,
		bs_read_exactly_from_buffer,
		bs_read_exactly_error};

	SET_CUSTOM_FAKE_SEQ(read_exactly, read_exactly_fakes, ARRAY_SIZE(read_exactly_fakes));
	bs_write_fake.custom_fake = bs_write_ok;

	ws->read_message(ws, read_handler, NULL);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg3_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg4_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "ws parameter in error callback not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, on_error_fake.arg1_val, "error callback called with wrong status code");
}


#if 0
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

static void close_with_no_reason(struct cio_websocket *s, uint8_t *data, size_t length, bool last_frame)
{
	(void)data;
	(void)length;
	(void)last_frame;
	ws->close(s, CIO_WEBSOCKET_CLOSE_GOING_AWAY, NULL);
}

static void test_close_in_textframe_callback(void)
{
	uint8_t data[] = "Hello";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_textframe_fake.custom_fake = close_with_no_reason;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(1, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_GOING_AWAY, true), "written frame is not a close frame!");
}

static void close_with_overlong_reason(struct cio_websocket *s, uint8_t *data, size_t length, bool last_frame)
{
	(void)data;
	(void)length;
	(void)last_frame;
	char buf[127];
	memset(buf, 0x00, sizeof(buf));
	buf[126] = '\0';
	ws->close(s, CIO_WEBSOCKET_CLOSE_GOING_AWAY, buf);
}

static void test_close_with_overlong_reason_in_textframe_callback(void)
{
	uint8_t data[] = "Hello";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;
	on_textframe_fake.custom_fake = close_with_overlong_reason;

	ws->internal_on_connect(ws);

	TEST_ASSERT_EQUAL_MESSAGE(1, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_close_fake.call_count, "close callback was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_GOING_AWAY, true), "written frame is not a close frame!");
}

static void test_send_text_binary_frame(void)
{
	uint32_t frame_sizes[] = {1, 5, 125, 126, 65535, 65536};
	unsigned int frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(frame_types); j++) {
			uint32_t frame_size = frame_sizes[i];
			char *data = malloc(frame_size);
			memset(data, 'a', frame_size);

			struct ws_frame frames[] = {
				{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
			};

			serialize_frames(frames, ARRAY_SIZE(frames));
			read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
			bs_write_fake.custom_fake = bs_write_ok;

			struct cio_write_buffer wbh;
			cio_write_buffer_head_init(&wbh);

			struct cio_write_buffer wb;
			cio_write_buffer_element_init(&wb, data, frame_size);
			cio_write_buffer_queue_tail(&wbh, &wb);

			uint32_t context = 0x1234568;
			if (frame_types[j] == CIO_WEBSOCKET_TEXT_FRAME) {
				enum cio_websocket_status status = ws->write_textframe(ws, &wbh, true, write_handler, &context);
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, status, "Writing a text frame did not succeed!");
				TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_TEXT_FRAME, data, frame_size, true), "First frame send is incorrect text frame!");
			} else if (frame_types[j] == CIO_WEBSOCKET_BINARY_FRAME) {
				enum cio_websocket_status status = ws->write_binaryframe(ws, &wbh, true, write_handler, &context);
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, status, "Writing a binary frame did not succeed!");
				TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_BINARY_FRAME, data, frame_size, true), "First frame send is incorrect binary frame!");
			}

			ws->internal_on_connect(ws);

			TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "written frame is not a close frame!");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
			TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was not called");
			TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

			TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write handler was not called!");
			TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, write_handler_fake.arg0_val, "websocket pointer in write handler not correct!");
			TEST_ASSERT_EQUAL_PTR_MESSAGE(&context, write_handler_fake.arg1_val, "context pointer in write handler not correct!");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_val, "error code in write handler not correct!");

			TEST_ASSERT_EQUAL_MESSAGE(1, wbh.data.q_len, "Length of write buffer different than before writing!");
			TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
			TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, wbh.next->data.element.data, frame_size, "Content of writebuffer not correct after writing!");

			if (data) {
				free(data);
			}

			setUp();
		}
	}

	free(ws);
}

#if 0
static void test_send_ping_frame_no_payload(void)
{
	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;


	uint32_t context = 0x1234568;
	enum cio_websocket_status status = ws->write_pingframe(ws, NULL, write_handler, &context);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, status, "Writing a binary frame did not succeed!");
	TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_PING_FRAME, NULL, 0, true), "First frame send is incorrect ping frame!");

	ws->internal_on_connect(ws);

	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "written frame is not a close frame!");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write handler was not called!");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, write_handler_fake.arg0_val, "websocket pointer in write handler not correct!");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(&context, write_handler_fake.arg1_val, "context pointer in write handler not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_val, "error code in write handler not correct!");
}
#endif

static void test_send_ping_frame(void)
{
	char buffer[] = "aaaaaaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));
	read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;
	bs_write_fake.custom_fake = bs_write_ok;

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	uint32_t context = 0x1234568;
	enum cio_websocket_status status = ws->write_pingframe(ws, &wbh, write_handler, &context);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, status, "Writing a binary frame did not succeed!");
	TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_PING_FRAME, buffer, sizeof(buffer), true), "First frame send is incorrect ping frame!");

	ws->internal_on_connect(ws);

	TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "written frame is not a close frame!");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_textframe_fake.call_count, "callback for text frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_binaryframe_fake.call_count, "callback for binary frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_ping_fake.call_count, "callback for ping frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_pong_fake.call_count, "callback for pong frames was called");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_close_fake.call_count, "close callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write handler was not called!");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, write_handler_fake.arg0_val, "websocket pointer in write handler not correct!");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(&context, write_handler_fake.arg1_val, "context pointer in write handler not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_val, "error code in write handler not correct!");

	TEST_ASSERT_EQUAL_MESSAGE(1, wbh.data.q_len, "Length of write buffer different than before writing!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buffer, wbh.next->data.element.data, sizeof(buffer), "Content of writebuffer not correct after writing!");
}
#endif
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
	RUN_TEST(test_ping_frame_payload_too_long_no_error_callback);
#if 0
	RUN_TEST(test_close_frame_pong_not_written);

#endif

	RUN_TEST(test_close_in_get_header);
	RUN_TEST(test_immediate_read_error_for_get_header);
	RUN_TEST(test_read_error_in_get_header);

	RUN_TEST(test_immediate_read_error_for_get_first_length);

	RUN_TEST(test_immediate_read_error_for_get_mask_or_payload);

	RUN_TEST(test_immediate_read_error_for_get_length);

	RUN_TEST(test_close_in_get_length);

	RUN_TEST(test_close_in_get_mask);
	RUN_TEST(test_read_error_in_get_mask);
#if 0


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

	RUN_TEST(test_close_in_textframe_callback);
	RUN_TEST(test_close_with_overlong_reason_in_textframe_callback);

	RUN_TEST(test_send_text_binary_frame);
	RUN_TEST(test_send_ping_frame);
	//RUN_TEST(test_send_ping_frame_no_payload);
#endif

	return UNITY_END();
}
