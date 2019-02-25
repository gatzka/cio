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
#include "cio_util.h"
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

static void read_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err, size_t frame_length, uint8_t *data, size_t length, bool last_frame, bool is_binary);
FAKE_VOID_FUNC(read_handler, struct cio_websocket *, void *, enum cio_error, size_t, uint8_t *, size_t, bool, bool)

static void on_connect(struct cio_websocket *s);
FAKE_VOID_FUNC(on_connect, struct cio_websocket *)

static void on_control(const struct cio_websocket *ws, enum cio_websocket_frame_type type, const uint8_t *data, uint_fast8_t length);
FAKE_VOID_FUNC(on_control, const struct cio_websocket *, enum cio_websocket_frame_type, const uint8_t *, uint_fast8_t)

static void on_error(const struct cio_websocket *ws, enum cio_error err, const char *reason);
FAKE_VOID_FUNC(on_error, const struct cio_websocket *, enum cio_error, const char *)

static void write_handler(struct cio_websocket *ws, void *context, enum cio_error err);
FAKE_VOID_FUNC(write_handler, struct cio_websocket *, void *, enum cio_error)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

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

#define READ_BUFFER_SIZE 150
#define FRAME_BUFFER_SIZE 140000
#define WRITE_BUFFER_SIZE 140000

struct memory_stream {
	struct cio_io_stream ios;

	uint8_t frame_buffer[FRAME_BUFFER_SIZE];
	size_t frame_buffer_read_pos;
	size_t frame_buffer_fill_pos;

	uint8_t write_buffer[WRITE_BUFFER_SIZE];
	size_t write_buffer_pos;
	size_t write_buffer_parse_pos;
};

static struct memory_stream ms;

static uint8_t read_buffer[READ_BUFFER_SIZE];
static uint8_t read_back_buffer[FRAME_BUFFER_SIZE];
static size_t read_back_buffer_pos = 0;


static enum cio_error ms_read_some(struct cio_io_stream *io_stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context)
{
	io_stream->read_buffer = buffer;
	io_stream->read_handler = handler;
	io_stream->read_handler_context = handler_context;
	return CIO_SUCCESS;
}

static enum cio_error ms_write_some(struct cio_io_stream *io_stream, const struct cio_write_buffer *buf, cio_io_stream_write_handler handler, void *handler_context)
{
	struct memory_stream *mem_stream = cio_container_of(io_stream, struct memory_stream, ios);
	size_t len = buf->data.q_len;
	size_t written = 0;
	for (unsigned int i = 0; i < len; i++) {
		buf = buf->next;
		written += buf->data.element.length;
		memcpy(&mem_stream->write_buffer[mem_stream->write_buffer_pos], buf->data.element.data, buf->data.element.length);
		mem_stream->write_buffer_pos += buf->data.element.length;
	}

	handler(io_stream, handler_context, buf, CIO_SUCCESS, written);
	return CIO_SUCCESS;
}

static void memory_stream_init(struct memory_stream *s)
{
	s->frame_buffer_read_pos = 0;
	s->frame_buffer_fill_pos = 0;
	s->write_buffer_pos = 0;
	s->write_buffer_parse_pos = 0;
	memset(s->write_buffer, 0x00, WRITE_BUFFER_SIZE);

	s->ios.read_some = ms_read_some;
	s->ios.write_some = ms_write_some;
}

static void run_eventloop_fake(void)
{

while (true) {
	if (ms.frame_buffer_read_pos >= ms.frame_buffer_fill_pos) {
		//ms.ios.read_handler(&ms.ios, ms.ios.read_handler_context, CIO_EOF, ms.ios.read_buffer);
		return;
	}

	size_t free_bytes = cio_read_buffer_space_available(ms.ios.read_buffer);
	size_t unread_bytes = ms.frame_buffer_fill_pos - ms.frame_buffer_read_pos;
	size_t bytes_to_read = MIN(free_bytes, unread_bytes);
	memcpy(ms.ios.read_buffer->add_ptr, &ms.frame_buffer[ms.frame_buffer_read_pos], bytes_to_read);
	ms.ios.read_buffer->add_ptr += bytes_to_read;
	ms.frame_buffer_read_pos += bytes_to_read;
	ms.ios.read_handler(&ms.ios, ms.ios.read_handler_context, CIO_SUCCESS, ms.ios.read_buffer);
}
}

static bool check_frame(enum cio_websocket_frame_type opcode, const char *payload, size_t payload_length, bool is_last_frame)
{
	uint8_t header = ms.write_buffer[ms.write_buffer_parse_pos++];
	if (((header & WS_HEADER_FIN) == WS_HEADER_FIN) != is_last_frame) {
		return false;
	}

	if ((header & WS_HEADER_RSV) != 0) {
		return false;
	}

	if ((header & opcode) != opcode) {
		return false;
	}

	uint8_t first_length = ms.write_buffer[ms.write_buffer_parse_pos++];
	bool is_masked = ((first_length & WS_MASK_SET) == WS_MASK_SET);
	first_length = (uint8_t)(first_length & ~WS_MASK_SET);

	uint64_t length;
	if (first_length == 126) {
		uint16_t len;
		memcpy(&len, &ms.write_buffer[ms.write_buffer_parse_pos], sizeof(len));
		ms.write_buffer_parse_pos += sizeof(len);
		len = cio_be16toh(len);
		length = len;
	} else if (first_length == 127) {
		uint64_t len;
		memcpy(&len, &ms.write_buffer[ms.write_buffer_parse_pos], sizeof(len));
		ms.write_buffer_parse_pos += sizeof(len);
		len = cio_be64toh(len);
		length = len;
	} else {
		length = first_length;
	}

	if (length != payload_length) {
		return false;
	}

	if (length > SIZE_MAX) {
		return false;
	}

	if (is_masked) {
		uint8_t mask[4];
		memcpy(mask, &ms.write_buffer[ms.write_buffer_parse_pos], sizeof(mask));
		ms.write_buffer_parse_pos += sizeof(mask);
		cio_websocket_mask(&ms.write_buffer[ms.write_buffer_parse_pos], (size_t)length, mask);
	}

	if (length > 0) {
		if (memcmp(&ms.write_buffer[ms.write_buffer_parse_pos], payload, (size_t)length) != 0) {
			return false;
		}
	}

	ms.write_buffer_parse_pos += payload_length;

	return true;
}

static bool is_close_frame(uint16_t status_code, bool status_code_required)
{
	if ((ms.write_buffer[ms.write_buffer_parse_pos] & WS_HEADER_FIN) != WS_HEADER_FIN) {
		return false;
	}

	if ((ms.write_buffer[ms.write_buffer_parse_pos++] & WS_CLOSE_FRAME) == 0) {
		return false;
	}

	uint8_t first_length = ms.write_buffer[ms.write_buffer_parse_pos++];
	bool is_masked = ((first_length & WS_MASK_SET) == WS_MASK_SET);
	first_length = (uint8_t)(first_length & ~WS_MASK_SET);

	if (first_length > CIO_WEBSOCKET_SMALL_FRAME_SIZE) {
		return false;
	}

	if ((first_length == 0) && (status_code_required)) {
		return false;
	}

	if (first_length == 1) {
		return false;
	}

	if (is_masked) {
		uint8_t mask[4];
		memcpy(mask, &ms.write_buffer[ms.write_buffer_parse_pos], sizeof(mask));
		ms.write_buffer_parse_pos += sizeof(mask);
		cio_websocket_mask(&ms.write_buffer[ms.write_buffer_parse_pos], first_length, mask);
	}

	if (first_length > 0) {
		uint16_t sc;
		memcpy(&sc, &ms.write_buffer[ms.write_buffer_parse_pos], sizeof(sc));
		sc = cio_be16toh(sc);
		ms.write_buffer_parse_pos += sizeof(sc);
		first_length = (uint8_t)(first_length - sizeof(sc));
		ms.write_buffer_parse_pos += first_length;
		if (status_code != sc) {
			return false;
		}
	}

	return true;
}

static void serialize_frames(struct ws_frame frames[], size_t num_frames)
{
	size_t buffer_pos = 0;
	for (size_t i = 0; i < num_frames; i++) {
		struct ws_frame frame = frames[i];
		if (frame.last_frame) {
			ms.frame_buffer[buffer_pos] = WS_HEADER_FIN;
		} else {
			ms.frame_buffer[buffer_pos] = 0x0;
		}

		if (frame.rsv) {
			ms.frame_buffer[buffer_pos] |= 0x70;
		}

		ms.frame_buffer[buffer_pos] = (uint8_t)(ms.frame_buffer[buffer_pos] | frame.frame_type);
		buffer_pos++;

		if (frame.direction == FROM_CLIENT) {
			ms.frame_buffer[buffer_pos] = WS_MASK_SET;
		} else {
			ms.frame_buffer[buffer_pos] = 0x00;
		}

		if (frame.data_length <= 125) {
			ms.frame_buffer[buffer_pos] |= (uint8_t)frame.data_length;
			buffer_pos++;
		} else if (frame.data_length < 65536) {
			uint16_t len = (uint16_t)frame.data_length;
			ms.frame_buffer[buffer_pos] |= 126;
			buffer_pos++;
			len = cio_htobe16(len);
			memcpy(&ms.frame_buffer[buffer_pos], &len, sizeof(len));
			buffer_pos += sizeof(len);
		} else {
			ms.frame_buffer[buffer_pos] |= 127;
			buffer_pos++;
			uint64_t len = (uint64_t)frame.data_length;
			len = cio_htobe64(len);
			memcpy(&ms.frame_buffer[buffer_pos], &len, sizeof(len));
			buffer_pos += sizeof(len);
		}

		uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
		if (frame.direction == FROM_CLIENT) {
			memcpy(&ms.frame_buffer[buffer_pos], mask, sizeof(mask));
			buffer_pos += sizeof(mask);
		}

		if (frame.data_length > 0) {
			memcpy(&ms.frame_buffer[buffer_pos], frame.data, frame.data_length);
			if (frame.direction == FROM_CLIENT) {
				cio_websocket_mask(&ms.frame_buffer[buffer_pos], frame.data_length, mask);
			}

			buffer_pos += frame.data_length;
		}
	}

	ms.frame_buffer_fill_pos = buffer_pos;
}

static void read_handler_save_data(struct cio_websocket *websocket, void *handler_context, enum cio_error err, size_t remaining_length, uint8_t *data, size_t length, bool last_frame, bool is_binary)
{
	(void)last_frame;
	(void)is_binary;

	if (err == CIO_SUCCESS) {
		if (length > 0) {
			memcpy(&read_back_buffer[read_back_buffer_pos], data, length);
			read_back_buffer_pos += length;
		}

		if (remaining_length == 0) {
			err = websocket->read_message(websocket, read_handler, handler_context);
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");
		}
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
	RESET_FAKE(on_connect);
	RESET_FAKE(on_control);
	RESET_FAKE(on_error);

	RESET_FAKE(write_handler);


	memory_stream_init(&ms);
	cio_read_buffer_init(&http_client.rb, read_buffer, sizeof(read_buffer));
	cio_buffered_stream_init(&http_client.bs, &ms.ios);
	ws = (struct cio_websocket *)malloc(sizeof(*ws));
	cio_websocket_init(ws, false, on_connect, NULL);
	ws->ws_private.http_client = &http_client;
	ws->on_control = on_control;
	ws->on_error = on_error;

	read_handler_fake.custom_fake = read_handler_save_data;
	on_control_fake.custom_fake = on_control_save_data;
	on_error_fake.custom_fake = on_error_save_data;

	memset(read_buffer, 0x00, sizeof(read_buffer));
	memset(read_back_buffer, 0x00, sizeof(read_back_buffer));
	read_back_buffer_pos = 0;
}

void tearDown(void)
{
	free(ws);
}

static void test_client_send_text_binary_frame(void)
{
	uint32_t frame_sizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 125, 126, 65535, 65536};
	unsigned int frame_types[] = {CIO_WEBSOCKET_BINARY_FRAME, CIO_WEBSOCKET_TEXT_FRAME};
	enum frame_direction dirs[] = {FROM_CLIENT, FROM_SERVER};

	for (unsigned int i = 0; i < ARRAY_SIZE(frame_sizes); i++) {
		for (unsigned int j = 0; j < ARRAY_SIZE(frame_types); j++) {
			for (unsigned int k = 0; k < ARRAY_SIZE(dirs); k++) {
				uint32_t frame_size = frame_sizes[i];
				enum frame_direction dir = dirs[k];
				char *data = malloc(frame_size);
				memset(data, 'a', frame_size);
				char *check_data = malloc(frame_size);
				memset(check_data, 'a', frame_size);

				struct ws_frame frames[] = {
				    {.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = dir, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
				};

				serialize_frames(frames, ARRAY_SIZE(frames));

				struct cio_write_buffer wbh;
				cio_write_buffer_head_init(&wbh);

				struct cio_write_buffer wb;
				cio_write_buffer_element_init(&wb, data, frame_size);
				cio_write_buffer_queue_tail(&wbh, &wb);

				uint32_t context = 0x1234568;
				if (frame_types[j] == CIO_WEBSOCKET_TEXT_FRAME) {
					enum cio_error err = ws->write_message(ws, &wbh, true, false, write_handler, &context);
					TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a text frame did not succeed!");
					TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_TEXT_FRAME, check_data, frame_size, true), "First frame send is incorrect text frame!");
				} else if (frame_types[j] == CIO_WEBSOCKET_BINARY_FRAME) {
					enum cio_error err = ws->write_message(ws, &wbh, true, true, write_handler, &context);
					TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Writing a binary frame did not succeed!");
					TEST_ASSERT_MESSAGE(check_frame(CIO_WEBSOCKET_BINARY_FRAME, check_data, frame_size, true), "First frame send is incorrect binary frame!");
				}

				ws->ws_private.ws_flags.is_server = (dir == FROM_CLIENT) ? 1 : 0;
				enum cio_error err = ws->read_message(ws, read_handler, NULL);
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

				run_eventloop_fake();

				TEST_ASSERT_MESSAGE(is_close_frame(CIO_WEBSOCKET_CLOSE_NORMAL, true), "written frame is not a close frame!");

				TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write handler was not called!");
				TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, write_handler_fake.arg0_val, "websocket pointer in write handler not correct!");
				TEST_ASSERT_EQUAL_PTR_MESSAGE(&context, write_handler_fake.arg1_val, "context pointer in write handler not correct!");
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg2_val, "error code in write handler not correct!");

				TEST_ASSERT_EQUAL_MESSAGE(1, wbh.data.q_len, "Length of write buffer different than before writing!");
				TEST_ASSERT_EQUAL_MESSAGE(&wbh, wbh.next->next, "Concatenation of write buffers no longer correct after writing!");
				if (frame_size > 0) {
					TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, wbh.next->data.element.data, frame_size, "Content of writebuffer not correct after writing!");
				}

				TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
				TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was called not for last close frame");

				free(ws);
				free(data);
				free(check_data);
				setUp();
			}
		}
	}
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
				enum cio_error err = ws->read_message(ws, read_handler, NULL);
				TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

				run_eventloop_fake();

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
					TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg6_history[l], "last_frame parameter of read_handler not true");
					TEST_ASSERT_EQUAL_MESSAGE(frame_type == CIO_WEBSOCKET_BINARY_FRAME, read_handler_fake.arg7_history[l], "is_binary argument not not correct");
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
					enum cio_error err = ws->read_message(ws, read_handler, NULL);
					TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

					run_eventloop_fake();

					TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(3, read_handler_fake.call_count, "read_handler was not called");

					unsigned int arg_history_counter = MIN(read_handler_fake.arg_history_len, read_handler_fake.call_count);
					for (unsigned int read_cnt = 0; read_cnt < arg_history_counter; read_cnt++) {
						TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_history[read_cnt], "websocket parameter of read_handler not correct");
						TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[read_cnt], "context parameter of read handler not NULL");
						if (read_cnt < read_handler_fake.call_count - 1) {
							TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[read_cnt], "error parameter of read_handler not CIO_SUCCESS");
							if (read_cnt == 0) {
								TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg6_history[read_cnt], "last_frame parameter of read_handler for first fragment not false");
							}
							TEST_ASSERT_EQUAL_MESSAGE((frame_type == CIO_WEBSOCKET_BINARY_FRAME), read_handler_fake.arg7_history[read_cnt], "is_binary parameter of read_handler for first fragment not correct");
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

static void test_receive_three_fragments(void)
{
	char data[12];
	memset(data, 'a', sizeof(data));

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = false, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CONTINUATION_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

	TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(ARRAY_SIZE(frames), read_handler_fake.call_count, "read_handler was not called");
	for (unsigned int i = 0; i < (ARRAY_SIZE(frames) - 1); i++) {
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[i], "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[i], "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[i], "err parameter of read_handler not correct");
		TEST_ASSERT_NOT_NULL_MESSAGE(read_handler_fake.arg4_history[i], "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), read_handler_fake.arg5_history[i], "length parameter of read_handler not correct");
		TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg7_history[i], "is_binary parameter of read_handler not correct");
		if (i == (ARRAY_SIZE(frames) - 2)) {
			TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg6_history[i], "last_frame parameter of read_handler not correct");
		} else {
			TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg6_history[i], "last_frame parameter of read_handler not correct");
		}
	}

	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[ARRAY_SIZE(frames) - 1], "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[ARRAY_SIZE(frames) - 1], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[ARRAY_SIZE(frames) - 1], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

	TEST_ASSERT_EQUAL_MESSAGE(4, read_handler_fake.call_count, "read_handler was not called");

	for (unsigned int i = 0; i < 3; i++) {
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[i], "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[i], "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[i], "err parameter of read_handler not correct");
		TEST_ASSERT_NOT_NULL_MESSAGE(read_handler_fake.arg4_history[i], "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), read_handler_fake.arg5_history[i], "length parameter of read_handler not correct");
		TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg7_history[i], "is_binary parameter of read_handler not correct");
		if (i < 2) {
			TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg6_history[i], "last_frame parameter of read_handler not correct");
		} else {
			TEST_ASSERT_TRUE_MESSAGE(read_handler_fake.arg6_history[i], "last_frame parameter of read_handler not correct");
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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

	TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(3, read_handler_fake.call_count, "number of read_handler callbacks called not correct");

	for (unsigned int i = 0; i < read_handler_fake.call_count - 1; i++) {
		TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[i], "websocket parameter of read_handler not correct");
		TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[i], "context parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_history[i], "err parameter of read_handler not correct");
		TEST_ASSERT_NOT_NULL_MESSAGE(read_handler_fake.arg4_history[i], "data parameter of read_handler not correct");
		TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), read_handler_fake.arg5_history[i], "length parameter of read_handler not correct");
		TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg6_history[i], "last_frame parameter of read_handler not correct");
		TEST_ASSERT_FALSE_MESSAGE(read_handler_fake.arg7_history[i], "is_binary parameter of read_handler not correct");
	}

	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_history[read_handler_fake.call_count - 1], "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[read_handler_fake.call_count - 1], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_handler_fake.call_count - 1], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for close frame");
}

static void test_text_frame_not_utf8(void)
{
	uint8_t data[] = {0xf8, 0x88, 0x80, 0x80, 0x80};

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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
	uint8_t data[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2d, 0xc2, 0xb5, 0x40, 0xc3}; //9fc3b6c3a4
	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_TEXT_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context of read handler not NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not CIO_SUCCESS");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of error handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error code in error handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for last close frame");
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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "number of read_handler callbacks called not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[read_handler_fake.call_count - 1], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_handler_fake.call_count - 1], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of on_error not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called for close frame");
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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "number of read_handler callbacks called not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_history[read_handler_fake.call_count - 1], "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_history[read_handler_fake.call_count - 1], "err parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "error callback was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, on_error_fake.arg0_val, "websocket parameter of on_error not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PROTOCOL_NOT_SUPPORTED, on_error_fake.arg1_val, "error code of on_error not correct");

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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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

static void test_init_without_ws(void)
{
	enum cio_error err = cio_websocket_init(NULL, true, on_connect, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no ws pointer provided");
}

static void test_init_without_on_connect(void)
{
	enum cio_error err = cio_websocket_init(ws, true, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no on_connect function provided");
}

static void test_read_message_no_websocket(void)
{
	enum cio_error err = ws->read_message(NULL, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no websocket pointer is provided!");
}

static void test_read_message_no_handler(void)
{
	enum cio_error err = ws->read_message(ws, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error code if no handler is provided!");
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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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

	ws->on_control = NULL;
	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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

	ws->on_error = NULL;
	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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

static void test_receive_pong_frame(void)
{
	char data[] = "aaaa";

	struct ws_frame frames[] = {
		{.frame_type = CIO_WEBSOCKET_PONG_FRAME, .direction = FROM_CLIENT, .data = data, .data_length = sizeof(data), .last_frame = true, .rsv = false},
		{.frame_type = CIO_WEBSOCKET_CLOSE_FRAME, .direction = FROM_CLIENT, .data = NULL, .data_length = 0, .last_frame = true, .rsv = false},
	};

	serialize_frames(frames, ARRAY_SIZE(frames));

	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

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

	ws->on_control = NULL;
	ws->ws_private.ws_flags.is_server = (frames[0].direction == FROM_CLIENT) ? 1 : 0;
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");

	run_eventloop_fake();

	TEST_ASSERT_EQUAL_MESSAGE(0, on_control_fake.call_count, "control callback was called");

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read_handler was not called");
	TEST_ASSERT_EQUAL_PTR_MESSAGE(ws, read_handler_fake.arg0_val, "websocket parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg1_val, "context parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, read_handler_fake.arg2_val, "error parameter of read_handler not correct");
	TEST_ASSERT_NULL_MESSAGE(read_handler_fake.arg4_val, "data parameter of read_handler not correct");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.arg5_val, "length parameter of read_handler not correct");

	TEST_ASSERT_EQUAL_MESSAGE(0, on_error_fake.call_count, "error callback was called");
}


int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_client_send_text_binary_frame);
	RUN_TEST(test_receive_unfragmented_frames);
	RUN_TEST(test_receive_fragmented_frames);
	RUN_TEST(test_receive_three_fragments);
	RUN_TEST(test_wrong_opcode_between_fragments);
	RUN_TEST(test_wrong_opcode_in_fragment);
	RUN_TEST(test_control_frame_within_fragment);
	RUN_TEST(test_binary_frame_within_text_fragments);
	RUN_TEST(test_text_frame_not_utf8);
	RUN_TEST(test_text_frame_utf8_no_complete_in_last_frame);

	RUN_TEST(test_rsv_bit_in_header);
	RUN_TEST(test_fragmented_control_frame);
	RUN_TEST(test_wrong_continuation_frame_without_correct_start_frame);
	RUN_TEST(test_wrong_fragment_start);
	RUN_TEST(test_illegal_opcode);
	RUN_TEST(test_no_mask_set_from_client);




	RUN_TEST(test_init_without_ws);
	RUN_TEST(test_init_without_on_connect);

	RUN_TEST(test_read_message_no_websocket);
	RUN_TEST(test_read_message_no_handler);

	RUN_TEST(test_receive_ping_frame);
	RUN_TEST(test_receive_ping_frame_no_callback);
	RUN_TEST(test_receive_ping_frame_no_payload);
	RUN_TEST(test_receive_ping_frame_payload_too_long);
	RUN_TEST(test_recieve_ping_frame_payload_too_long_no_error_callback);
	RUN_TEST(test_receive_pong_frame);
	RUN_TEST(test_receive_pong_frame_no_callback);

	return UNITY_END();
}
