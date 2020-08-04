/*
 * SPDX-License-Identifier: MIT
 *
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

#include <stdlib.h>
#include <string.h>

#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_io_stream.h"
#include "cio_read_buffer.h"
#include "cio_string.h"
#include "cio_util.h"
#include "cio_write_buffer.h"
#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

#define CIO_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#undef MAX
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

#define CHUNK1 "Hello"
#define CHUNK2 "World"

struct memory_stream {
	struct cio_io_stream ios;
	void *mem;
	size_t read_pos;
	size_t size;
};

static uint8_t first_check_buffer[100];
static uint8_t write_check_buffer[100];
static size_t first_check_buffer_pos = 0;
static size_t write_check_buffer_pos = 0;

static uint8_t second_check_buffer[100];
static size_t second_check_buffer_pos = 0;
static size_t chunk_bytes_written = 0;

enum cio_error read_some(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *context);
FAKE_VALUE_FUNC(enum cio_error, read_some, struct cio_io_stream *, struct cio_read_buffer *, cio_io_stream_read_handler_t, void *)

enum cio_error write_some(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, write_some, struct cio_io_stream *, struct cio_write_buffer *, cio_io_stream_write_handler_t, void *)

enum cio_error close(struct cio_io_stream *context);
FAKE_VALUE_FUNC(enum cio_error, close, struct cio_io_stream *)

void dummy_read_handler(struct cio_buffered_stream *, void *, enum cio_error, struct cio_read_buffer *, size_t);
FAKE_VOID_FUNC(dummy_read_handler, struct cio_buffered_stream *, void *, enum cio_error, struct cio_read_buffer *, size_t)

void second_dummy_read_handler(struct cio_buffered_stream *, void *, enum cio_error, struct cio_read_buffer *, size_t);
FAKE_VOID_FUNC(second_dummy_read_handler, struct cio_buffered_stream *, void *, enum cio_error, struct cio_read_buffer *, size_t)

void dummy_write_handler(struct cio_buffered_stream *, void *, enum cio_error);
FAKE_VOID_FUNC(dummy_write_handler, struct cio_buffered_stream *, void *, enum cio_error)

struct client {
	struct memory_stream ms;
	struct cio_buffered_stream bs;
};

static void memory_stream_deinit(struct memory_stream *ms)
{
	free(ms->mem);
}

static enum cio_error client_close(struct cio_io_stream *ios)
{
	struct memory_stream *memory_stream = cio_container_of(ios, struct memory_stream, ios);
	memory_stream_deinit(memory_stream);

	struct client *client = cio_container_of(memory_stream, struct client, ms);

	close(ios);
	free(client);
	return CIO_SUCCESS;
}

static void memory_stream_init(struct memory_stream *ms, const char *fill_pattern)
{
	ms->read_pos = 0;
	ms->size = strlen(fill_pattern);
	ms->ios.read_some = read_some;
	ms->ios.write_some = write_some;
	ms->ios.close = client_close;
	ms->mem = malloc(ms->size + 1);
	memset(ms->mem, 0x00, ms->size + 1);
	memcpy(ms->mem, fill_pattern, ms->size);
}

void setUp(void)
{
	FFF_RESET_HISTORY()

	RESET_FAKE(read_some)
	RESET_FAKE(write_some)
	RESET_FAKE(close)
	RESET_FAKE(dummy_read_handler)
	RESET_FAKE(second_dummy_read_handler)
	RESET_FAKE(dummy_write_handler)

	memset(first_check_buffer, 0xaf, sizeof(first_check_buffer));
	memset(write_check_buffer, 0xaf, sizeof(write_check_buffer));
	first_check_buffer_pos = 0;
	write_check_buffer_pos = 0;
	memset(second_check_buffer, 0xaf, sizeof(second_check_buffer));
	second_check_buffer_pos = 0;
	chunk_bytes_written = 0;
}

void tearDown(void)
{
}

static void write_then_read(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	struct cio_read_buffer *rb = handler_context;

	err = cio_buffered_stream_read_at_least(bs, rb, 1, dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
}

static enum cio_error write_some_all(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	struct cio_write_buffer *wb = buf->next;
	size_t total_length = 0;

	for (size_t i = 0; i < cio_write_buffer_get_num_buffer_elements(buf); i++) {
		if (wb->data.element.length > 0) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length);
		}

		write_check_buffer_pos += wb->data.element.length;
		total_length += wb->data.element.length;
		wb = wb->next;
	}

	handler(io_stream, handler_context, buf, CIO_SUCCESS, total_length);

	return CIO_SUCCESS;
}

static enum cio_error write_some_first_write_partial_second_error(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	struct cio_write_buffer *wbh = buf;
	if (write_some_fake.call_count == 1) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 1) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length / 2);
			write_check_buffer_pos += wb->data.element.length / 2;
		}

		handler(io_stream, handler_context, buf, CIO_SUCCESS, wb->data.element.length / 2);
	} else if (write_some_fake.call_count == 2) {
		handler(io_stream, handler_context, wbh, CIO_MESSAGE_TOO_LONG, 0);
	} else {
		write_some_all(io_stream, wbh, handler, handler_context);
	}

	return CIO_SUCCESS;
}

static enum cio_error write_some_first_write_partial_at_element_boundary_second_error(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	struct cio_write_buffer *wbh = buf;
	if (write_some_fake.call_count == 1) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 1) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length);
			write_check_buffer_pos += wb->data.element.length;
		}

		handler(io_stream, handler_context, wbh, CIO_SUCCESS, wb->data.element.length);
	} else if (write_some_fake.call_count == 2) {
		handler(io_stream, handler_context, wbh, CIO_MESSAGE_TOO_LONG, 0);
	} else {
		write_some_all(io_stream, wbh, handler, handler_context);
	}

	return CIO_SUCCESS;
}

static enum cio_error write_some_first_write_partial_second_error_sync(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (write_some_fake.call_count == 1) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 1) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length / 2);
			write_check_buffer_pos += wb->data.element.length / 2;
		}

		handler(io_stream, handler_context, buf, CIO_SUCCESS, wb->data.element.length / 2);
	} else if (write_some_fake.call_count == 2) {
		return CIO_INVALID_ARGUMENT;
	} else {
		write_some_all(io_stream, buf, handler, handler_context);
	}

	return CIO_SUCCESS;
}

static enum cio_error write_some_first_writes_partial_third_error_sync(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (write_some_fake.call_count < 3) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 1) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length / 2);
			write_check_buffer_pos += wb->data.element.length / 2;
		}

		handler(io_stream, handler_context, buf, CIO_SUCCESS, wb->data.element.length / 2);
	} else if (write_some_fake.call_count == 3) {
		return CIO_INVALID_ARGUMENT;
	} else {
		write_some_all(io_stream, buf, handler, handler_context);
	}

	return CIO_SUCCESS;
}

static enum cio_error write_some_error(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	handler(io_stream, handler_context, buf, CIO_MESSAGE_TOO_LONG, 0);
	return CIO_SUCCESS;
}

static enum cio_error write_some_error_sync(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	(void)io_stream;
	(void)buf;
	(void)handler;
	(void)handler_context;

	return CIO_INVALID_ARGUMENT;
}

static enum cio_error write_some_double_write_partial(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (write_some_fake.call_count <= 2) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 1) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length / 2);
			write_check_buffer_pos += wb->data.element.length / 2;
		}

		handler(io_stream, handler_context, buf, CIO_SUCCESS, wb->data.element.length / 2);
	} else {
		write_some_all(io_stream, buf, handler, handler_context);
	}

	return CIO_SUCCESS;
}

static enum cio_error write_some_first_write_partial_at_buffer_boundary(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (write_some_fake.call_count == 1) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 1) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length);
			write_check_buffer_pos += wb->data.element.length;
		}

		handler(io_stream, handler_context, buf, CIO_SUCCESS, wb->data.element.length);
	} else {
		write_some_all(io_stream, buf, handler, handler_context);
	}

	return CIO_SUCCESS;
}

static void save_to_check_buffer(struct cio_buffered_stream *bs, void *context, enum cio_error err, struct cio_read_buffer *buffer, size_t num_bytes)
{
	(void)bs;
	(void)context;

	if (err == CIO_SUCCESS) {
		uint8_t *read_ptr = cio_read_buffer_get_read_ptr(buffer);
		memcpy(&first_check_buffer[first_check_buffer_pos], read_ptr, num_bytes);
		cio_read_buffer_consume(buffer, num_bytes);
		first_check_buffer_pos += num_bytes;
	}
}

static void save_to_check_buffer_and_close(struct cio_buffered_stream *bs, void *context, enum cio_error err, struct cio_read_buffer *buffer, size_t num_bytes)
{
	save_to_check_buffer(bs, context, err, buffer, num_bytes);
	cio_buffered_stream_close(bs);
}

static void save_to_second_check_buffer(struct cio_buffered_stream *bs, void *context, enum cio_error err, struct cio_read_buffer *buffer, size_t num_bytes)
{
	(void)bs;
	(void)context;

	if (err == CIO_SUCCESS) {
		uint8_t *read_ptr = cio_read_buffer_get_read_ptr(buffer);
		memcpy(&second_check_buffer[second_check_buffer_pos], read_ptr, num_bytes);
		second_check_buffer_pos += num_bytes;
	}
}

static void save_to_second_check_buffer_and_close(struct cio_buffered_stream *bs, void *context, enum cio_error err, struct cio_read_buffer *buffer, size_t num_bytes)
{
	(void)num_bytes;
	size_t available = cio_read_buffer_unread_bytes(buffer);
	save_to_second_check_buffer(bs, context, err, buffer, available);
	cio_buffered_stream_close(bs);
}

static void save_to_check_buffer_and_read_at_least_again(struct cio_buffered_stream *bs, void *context, enum cio_error err, struct cio_read_buffer *buffer, size_t num_bytes)
{
	char *second_chunk = context;

	save_to_check_buffer(bs, context, err, buffer, num_bytes);
	err = cio_buffered_stream_read_at_least(bs, buffer, strlen(second_chunk), second_dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Call to read_at_least did not succeed!");
}

static void save_to_check_buffer_and_read_until_again(struct cio_buffered_stream *bs, void *context, enum cio_error err, struct cio_read_buffer *buffer, size_t num_bytes)
{
	char *second_delim = context;

	save_to_check_buffer(bs, context, err, buffer, num_bytes);
	err = cio_buffered_stream_read_until(bs, buffer, second_delim, second_dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Call to read_at_least did not succeed!");
}

static void save_to_check_buffer_and_read_again(struct cio_buffered_stream *bs, void *context, enum cio_error err, struct cio_read_buffer *buffer, size_t num_bytes)
{
	(void)context;
	(void)num_bytes;

	size_t available = cio_read_buffer_unread_bytes(buffer);
	save_to_check_buffer(bs, context, err, buffer, available);
	err = cio_buffered_stream_read_at_least(bs, buffer, 1, second_dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Call to read_at_least did not succeed!");
}

static enum cio_error read_some_max(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *context)
{
	struct memory_stream *memory_stream = cio_container_of(ios, struct memory_stream, ios);
	size_t len = CIO_MIN(cio_read_buffer_size(buffer), memory_stream->size - memory_stream->read_pos);
	memcpy(buffer->data, &((uint8_t *)memory_stream->mem)[memory_stream->read_pos], len);
	memory_stream->read_pos += len;
	buffer->add_ptr += len;
	handler(ios, context, CIO_SUCCESS, buffer);
	return CIO_SUCCESS;
}

static enum cio_error read_some_chunks(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *context)
{
	struct memory_stream *memory_stream = cio_container_of(ios, struct memory_stream, ios);
	size_t string_len = strlen(memory_stream->mem);
	if (read_some_fake.call_count == 1) {
		string_len = string_len / 2;
		size_t len = CIO_MIN(cio_read_buffer_space_available(buffer), string_len);
		memcpy(buffer->add_ptr, memory_stream->mem, len);
		buffer->add_ptr += len;
		chunk_bytes_written += len;
		handler(ios, context, CIO_SUCCESS, buffer);
	} else {
		string_len = string_len - chunk_bytes_written;
		size_t len = CIO_MIN(cio_read_buffer_space_available(buffer), string_len);
		memcpy(buffer->add_ptr, (uint8_t *)memory_stream->mem + chunk_bytes_written, len);
		buffer->add_ptr += len;
		chunk_bytes_written += len;
		handler(ios, context, CIO_SUCCESS, buffer);
	}

	return CIO_SUCCESS;
}

static enum cio_error write_some_first_write_partial(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (write_some_fake.call_count == 1) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 1) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length / 2);
			write_check_buffer_pos += wb->data.element.length / 2;
		}

		handler(io_stream, handler_context, buf, CIO_SUCCESS, wb->data.element.length / 2);
	} else {
		write_some_all(io_stream, buf, handler, handler_context);
	}

	return CIO_SUCCESS;
}

static enum cio_error write_some_first_and_second_write_partial(struct cio_io_stream *io_stream, struct cio_write_buffer *buf, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (write_some_fake.call_count == 1) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 1) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length / 2);
			write_check_buffer_pos += wb->data.element.length / 2;
		}

		handler(io_stream, handler_context, buf, CIO_SUCCESS, wb->data.element.length / 2);
	} else if (write_some_fake.call_count == 2) {
		struct cio_write_buffer *wb = buf->next;
		if (cio_write_buffer_get_num_buffer_elements(buf) >= 2) {
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length);
			write_check_buffer_pos += wb->data.element.length;
			size_t written = wb->data.element.length;

			wb = wb->next;
			memcpy(&write_check_buffer[write_check_buffer_pos], wb->data.element.const_data, wb->data.element.length / 2);
			write_check_buffer_pos += wb->data.element.length / 2;
			written += (wb->data.element.length / 2);
			handler(io_stream, handler_context, buf, CIO_SUCCESS, written);
		}
	} else {
		write_some_all(io_stream, buf, handler, handler_context);
	}

	return CIO_SUCCESS;
}

static enum cio_error read_some_error(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *context)
{
	handler(ios, context, CIO_INVALID_ARGUMENT, buffer);
	return CIO_SUCCESS;
}

static enum cio_error read_some_error_sync(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *context)
{
	(void)ios;
	(void)handler;
	(void)context;
	(void)buffer;

	return CIO_INVALID_ARGUMENT;
}

static void test_init_missing_bs_pointer(void)
{
	struct cio_io_stream ios;
	uint32_t buffer;
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(NULL, &ios);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Wrong initialization of buffered_stream does not return an error!");
}

static void test_init_missing_stream(void)
{
	struct cio_buffered_stream bs;

	uint32_t buffer;
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&bs, NULL);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Wrong initialization of buffered_stream does not return an error!");
}

static void test_init_correctly(void)
{
	struct client *client = malloc(sizeof(*client));
	memory_stream_init(&client->ms, "foo");

	uint32_t buffer;
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Correct initialization of buffered_stream returned an error!");
	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_least(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[100];

	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, strlen(test_data), dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, test_data, strlen(test_data)) == 0, "Handler was not called with correct data!");
}

static void test_read_at_least_close_in_callback(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer_and_close;

	uint8_t buffer[100];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, strlen(test_data), dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, test_data, strlen(test_data)) == 0, "Handler was not called with correct data!");
}

static void test_read_at_least_zero_length(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[100];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, 0, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
}

static void test_read_at_least_more_than_buffer_size(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, sizeof(buffer) + 1, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_least_no_buffered_stream(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(NULL, &rb, sizeof(buffer), dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_least_no_handler(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, sizeof(buffer), NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_least_no_buffer(void)
{
	struct client *client = malloc(sizeof(*client));

	size_t read_buffer_size = 40;
	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, NULL, read_buffer_size, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_least_ios_error(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_error;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, sizeof(buffer) - 1, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_INVALID_ARGUMENT!");
}

static void test_read_at_least_sync_error(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_error_sync;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer_and_close;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, sizeof(buffer) - 1, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	//err = cio_buffered_stream_close(&client->bs);
	//TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_INVALID_ARGUMENT!");
}

static void test_read_at_least_chunks(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "HelloWorld!";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_chunks;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	size_t first_chunk = 2;
	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, first_chunk, dummy_read_handler, &first_chunk);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_history[0], "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_history[0], "Handler was not called with original read buffer!");

	size_t second_chunk = strlen(test_data) - first_chunk;
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, second_chunk, dummy_read_handler, &second_chunk);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(2, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_history[1], "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_history[1], "Handler was not called with original read buffer!");

	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, test_data, strlen(test_data)) == 0, "Handler was not called with correct data!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_least_second_read_in_callback(void)
{
	struct client *client = malloc(sizeof(*client));

	char chunk2_buf[] = CHUNK2;
	static const char *test_data = CHUNK1 CHUNK2;
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer_and_read_at_least_again;
	second_dummy_read_handler_fake.custom_fake = save_to_second_check_buffer;

	uint8_t buffer[100];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_least(&client->bs, &rb, strlen(CHUNK1), dummy_read_handler, chunk2_buf);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, CHUNK1, strlen(CHUNK1)) == 0, "Handler was not called with correct data!");

	TEST_ASSERT_EQUAL_MESSAGE(1, second_dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, second_dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, second_dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)second_check_buffer, CHUNK2, strlen(CHUNK2)) == 0, "Handler was not called with correct data!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of close not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_most(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[100];

	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_most(&client->bs, &rb, strlen(test_data), dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, test_data, strlen(test_data)) == 0, "Handler was not called with correct data!");
}

static void test_read_at_most_more_than_buffer_size(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[100];

	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_most(&client->bs, &rb, strlen(test_data) + 1, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, test_data, strlen(test_data)) == 0, "Handler was not called with correct data!");
}

static void test_read_at_most_no_buffered_stream(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_most(NULL, &rb, sizeof(buffer), dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_most_no_buffer(void)
{
	struct client *client = malloc(sizeof(*client));

	size_t read_buffer_size = 40;
	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_most(&client->bs, NULL, read_buffer_size, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_most_no_handler(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_most(&client->bs, &rb, sizeof(buffer), NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_most_ios_error(void)
{
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = "Hello";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_error;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_read_at_most(&client->bs, &rb, sizeof(buffer) - 1, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_INVALID_ARGUMENT!");
}

static void test_read_until(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(&client->bs, &rb, DELIM, dummy_read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, PRE_DELIM DELIM, strlen(PRE_DELIM) + strlen(DELIM)) == 0, "Handler was not called with correct data!");
}

static void test_read_until_and_close(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer_and_close;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(&client->bs, &rb, DELIM, dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, PRE_DELIM DELIM, strlen(PRE_DELIM) + strlen(DELIM)) == 0, "Handler was not called with correct data!");
}

static void test_read_until_not_found(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	size_t read_buffer_size = strlen(test_data) - 1;
	uint8_t *buffer = malloc(read_buffer_size);
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, buffer, read_buffer_size);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(&client->bs, &rb, "Morning", dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	free(buffer);
}

static void test_read_until_zero_length_delim(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(&client->bs, &rb, "", dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
}

static void test_read_until_NULL_delim(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(&client->bs, &rb, NULL, dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_until_no_buffered_stream(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(NULL, &rb, DELIM, dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_until_no_buffer(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(&client->bs, NULL, DELIM, dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_until_no_handler(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(&client->bs, &rb, DELIM, NULL, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_read_handler_fake.call_count, "Handler was not called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_until_second_read_in_callback(void)
{
#define PRE_DELIM1 "MY"
#define DELIM1 "HelloWorld"
#define PRE_DELIM2 "Your"
#define DELIM2 "Stuff"
	char delim2[] = DELIM2;

	struct client *client = malloc(sizeof(*client));

	static const char *test_data = PRE_DELIM1 DELIM1 PRE_DELIM2 DELIM2 "Example";
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer_and_read_until_again;
	second_dummy_read_handler_fake.custom_fake = save_to_second_check_buffer;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_until(&client->bs, &rb, DELIM1, dummy_read_handler, delim2);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, PRE_DELIM1 DELIM1, strlen(PRE_DELIM1) + strlen(DELIM1)) == 0, "Handler was not called with correct data!");

	TEST_ASSERT_EQUAL_MESSAGE(1, second_dummy_read_handler_fake.call_count, "Second handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, second_dummy_read_handler_fake.arg2_val, "Second handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, second_dummy_read_handler_fake.arg3_val, "Second handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)second_check_buffer, PRE_DELIM2 DELIM2, strlen(PRE_DELIM2) + strlen(DELIM2)) == 0, "Second handler was not called with correct data!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_read_at_least_then_until(void)
{
#define BUFFER_FOR_EXACTLY "hhhhhhhhhhhhh"
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
#define REST "Example"
	struct client *client = malloc(sizeof(*client));

	static const char *test_data = BUFFER_FOR_EXACTLY PRE_DELIM DELIM REST;
	memory_stream_init(&client->ms, test_data);
	read_some_fake.custom_fake = read_some_chunks;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer;

	size_t buffer_length = strlen(BUFFER_FOR_EXACTLY PRE_DELIM DELIM) - 2;
	void *buffer = malloc(buffer_length);
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, buffer, buffer_length);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_read_at_least(&client->bs, &rb, strlen(BUFFER_FOR_EXACTLY), dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	err = cio_buffered_stream_read_until(&client->bs, &rb, DELIM, dummy_read_handler, first_check_buffer);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(2, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_history[0], "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_history[0], "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)first_check_buffer, BUFFER_FOR_EXACTLY, strlen(BUFFER_FOR_EXACTLY)) == 0, "Handler was not called with correct data!");

	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_history[1], "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_history[1], "Handler was not called with original read buffer!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)&first_check_buffer[strlen(BUFFER_FOR_EXACTLY)], PRE_DELIM DELIM, strlen(PRE_DELIM) + strlen(DELIM)) == 0, "Handler was not called with correct data!");

	free(buffer);
}

static void test_write_two_buffers_partial_write(void)
{
	static const char *test_data = "HelloWorld";
	struct client *client = malloc(sizeof(*client));
	int dummy_context;

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, test_data, strlen(test_data) / 2);
	cio_write_buffer_queue_tail(&wbh, &wb1);
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, test_data + (strlen(test_data) / 2), strlen(test_data) - strlen(test_data) / 2);
	cio_write_buffer_queue_tail(&wbh, &wb2);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_first_write_partial;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, &dummy_context);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(&client->bs, dummy_write_handler_fake.arg0_val, "Handler was not called with original buffered_stream!");
	TEST_ASSERT_EQUAL_MESSAGE(&dummy_context, dummy_write_handler_fake.arg1_val, "Handler was not called with correct context!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)write_check_buffer, test_data, strlen(test_data)) == 0, "Data was not written correctly!");
}

static void test_write_three_buffers_two_partial_writes(void)
{
#define test_data_chunk "Hello"
	static const char test_data[] = test_data_chunk test_data_chunk test_data_chunk;
	struct client *client = malloc(sizeof(*client));
	int dummy_context;

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, test_data, strlen(test_data_chunk));
	cio_write_buffer_queue_tail(&wbh, &wb1);
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, test_data + (strlen(test_data_chunk)), strlen(test_data_chunk));
	cio_write_buffer_queue_tail(&wbh, &wb2);
	struct cio_write_buffer wb3;
	cio_write_buffer_const_element_init(&wb3, test_data + (2 * (strlen(test_data_chunk))), strlen(test_data_chunk));
	cio_write_buffer_queue_tail(&wbh, &wb3);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_first_and_second_write_partial;

	uint8_t buffer[40];
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, &dummy_context);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(&client->bs, dummy_write_handler_fake.arg0_val, "Handler was not called with original buffered_stream!");
	TEST_ASSERT_EQUAL_MESSAGE(&dummy_context, dummy_write_handler_fake.arg1_val, "Handler was not called with correct context!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)write_check_buffer, test_data, strlen(test_data)) == 0, "Data was not written correctly!");
}

static void test_write_one_buffer_one_chunk(void)
{
	static const char *test_data = "Hello";
	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_all;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)write_check_buffer, test_data, strlen(test_data)) == 0, "Data was not written correctly!");
}

static void test_write_one_buffer_one_chunk_read_in_callbacks_then_close(void)
{
	static const char *test_data = "Hello";
	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	write_some_fake.custom_fake = write_some_all;

	static const char *read_test_data = CHUNK1 CHUNK2;
	memory_stream_init(&client->ms, read_test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_read_handler_fake.custom_fake = save_to_check_buffer_and_read_again;
	second_dummy_read_handler_fake.custom_fake = save_to_second_check_buffer_and_close;

	size_t read_buffer_size = MAX(strlen(CHUNK1), strlen(CHUNK2));
	uint8_t *buffer = malloc(read_buffer_size);
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, buffer, read_buffer_size);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");

	dummy_write_handler_fake.custom_fake = write_then_read;

	err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, &rb);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)write_check_buffer, test_data, strlen(test_data)) == 0, "Data was not written correctly!");

	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_read_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_read_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, dummy_read_handler_fake.arg3_val, "Handler was not called with original read buffer!");

	TEST_ASSERT_EQUAL_MESSAGE(1, second_dummy_read_handler_fake.call_count, "Second handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, second_dummy_read_handler_fake.arg2_val, "Second handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, second_dummy_read_handler_fake.arg3_val, "Second handler was not called with original read buffer!");

	char *result = malloc(strlen(CHUNK1) + strlen(CHUNK2) + 1);
	memcpy(result, first_check_buffer, first_check_buffer_pos);
	memcpy(&result[first_check_buffer_pos], second_check_buffer, second_check_buffer_pos);
	TEST_ASSERT_MESSAGE(memcmp(result, CHUNK1 CHUNK2, strlen(CHUNK1 CHUNK2)) == 0, "Handler was not called with correct data!");
	free(result);

	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	free(buffer);
}

static void test_write_two_buffers_double_partial_write(void)
{
	static const char *test_data = "HelloWorld";
	int dummy_context;

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, test_data, strlen(test_data) / 2);
	cio_write_buffer_queue_tail(&wbh, &wb1);
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, test_data + (strlen(test_data) / 2), strlen(test_data) - strlen(test_data) / 2);
	cio_write_buffer_queue_tail(&wbh, &wb2);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_double_write_partial;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, &dummy_context);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(&client->bs, dummy_write_handler_fake.arg0_val, "Handler was not called with original buffered_stream!");
	TEST_ASSERT_EQUAL_MESSAGE(&dummy_context, dummy_write_handler_fake.arg1_val, "Handler was not called with correct context!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)write_check_buffer, test_data, strlen(test_data)) == 0, "Data was not written correctly!");
}

static void test_write_two_buffers_partial_write_at_buffer_boundary(void)
{
	static const char *test_data = "HelloWorld";
	int dummy_context;

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, test_data, strlen(test_data) / 2);
	cio_write_buffer_queue_tail(&wbh, &wb1);
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, test_data + (strlen(test_data) / 2), strlen(test_data) - strlen(test_data) / 2);
	cio_write_buffer_queue_tail(&wbh, &wb2);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_first_write_partial_at_buffer_boundary;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, &dummy_context);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(&client->bs, dummy_write_handler_fake.arg0_val, "Handler was not called with original buffered_stream!");
	TEST_ASSERT_EQUAL_MESSAGE(&dummy_context, dummy_write_handler_fake.arg1_val, "Handler was not called with correct context!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)write_check_buffer, test_data, strlen(test_data)) == 0, "Data was not written correctly!");
}

static void test_write_one_buffer_one_chunk_error(void)
{
	static const char *test_data = "Hello";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_error;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_MESSAGE_TOO_LONG!");
}

static void test_write_one_buffer_one_chunk_error_sync(void)
{
	static const char *test_data = "Hello";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_error_sync;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_write_handler_fake.call_count, "Handler was not called!");
}

static void test_write_no_buffered_stream_for_write(void)
{
	static const char *test_data = "Hello";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_all;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(NULL, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_write_handler_fake.call_count, "Handler was called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_write_no_buffer_for_write(void)
{
	static const char *test_data = "Hello";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_all;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, NULL, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_write_handler_fake.call_count, "Handler was called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_write_no_handler_for_write(void)
{
	static const char *test_data = "Hello";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_all;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_write_handler_fake.call_count, "Handler was called!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

static void test_write_one_buffer_partial_write_error(void)
{
	static const char *test_data = "Hello";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_first_write_partial_second_error;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_MESSAGE_TOO_LONG!");
}

static void test_write_one_buffer_partial_write_error_at_element_boundary(void)
{
	static const char *test_data1 = "Hello";
	static const char *test_data2 = "World";
	static const char *test_data3 = "foo";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, test_data1, strlen(test_data1));
	cio_write_buffer_queue_tail(&wbh, &wb1);

	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, test_data2, strlen(test_data2));
	cio_write_buffer_queue_tail(&wbh, &wb2);

	struct cio_write_buffer wb3;
	cio_write_buffer_const_element_init(&wb3, test_data3, strlen(test_data3));
	cio_write_buffer_queue_tail(&wbh, &wb3);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_first_write_partial_at_element_boundary_second_error;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_MESSAGE_TOO_LONG, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_MESSAGE_TOO_LONG!");
}

static void test_write_one_buffer_partial_write_error_sync(void)
{
	static const char *test_data = "Hello";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_first_write_partial_second_error_sync;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_INVALID_ARGUMENT!");
}

static void test_write_one_buffer_partial_write_third_write_error_sync(void)
{
	static const char *test_data = "HelloHelloHello";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_first_writes_partial_third_error_sync;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_INVALID_ARGUMENT!");
}

static void test_write_two_buffers_one_chunk(void)
{
	static const char *test_data = "HelloWorld";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, test_data, strlen(test_data) / 2);
	cio_write_buffer_queue_tail(&wbh, &wb1);
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, test_data + (strlen(test_data) / 2), strlen(test_data) - strlen(test_data) / 2);
	cio_write_buffer_queue_tail(&wbh, &wb2);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_all;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)write_check_buffer, test_data, strlen(test_data)) == 0, "Data was not written correctly!");
}

static void test_write_two_buffers_one_chunk_last_buffer_empty(void)
{
	static const char *test_data = "HelloWorld";

	struct client *client = malloc(sizeof(*client));

	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, test_data, strlen(test_data));
	cio_write_buffer_queue_tail(&wbh, &wb1);
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, NULL, 0);
	cio_write_buffer_queue_tail(&wbh, &wb2);

	memory_stream_init(&client->ms, "");
	write_some_fake.custom_fake = write_some_all;

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");
	err = cio_buffered_stream_write(&client->bs, &wbh, dummy_write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_write_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, dummy_write_handler_fake.arg2_val, "Handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_MESSAGE(memcmp((const char *)write_check_buffer, test_data, strlen(test_data)) == 0, "Data was not written correctly!");
}

static void test_close_no_stream(void)
{
	struct client *client = malloc(sizeof(*client));
	memory_stream_init(&client->ms, "");

	enum cio_error err = cio_buffered_stream_init(&client->bs, &client->ms.ios);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Buffer was not initialized correctly!");

	err = cio_buffered_stream_close(NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, close_fake.call_count, "Underlying cio_iostream was not closed!");

	err = cio_buffered_stream_close(&client->bs);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_missing_bs_pointer);
	RUN_TEST(test_init_missing_stream);
	RUN_TEST(test_init_correctly);

	RUN_TEST(test_read_at_least);
	RUN_TEST(test_read_at_least_close_in_callback);
	RUN_TEST(test_read_at_least_more_than_buffer_size);
	RUN_TEST(test_read_at_least_no_buffered_stream);
	RUN_TEST(test_read_at_least_no_handler);
	RUN_TEST(test_read_at_least_no_buffer);
	RUN_TEST(test_read_at_least_ios_error);
	RUN_TEST(test_read_at_least_sync_error);
	RUN_TEST(test_read_at_least_chunks);
	RUN_TEST(test_read_at_least_zero_length);
	RUN_TEST(test_read_at_least_second_read_in_callback);

	RUN_TEST(test_read_at_most);
	RUN_TEST(test_read_at_most_more_than_buffer_size);
	RUN_TEST(test_read_at_most_no_buffered_stream);
	RUN_TEST(test_read_at_most_no_buffer);
	RUN_TEST(test_read_at_most_no_handler);
	RUN_TEST(test_read_at_most_ios_error);

	RUN_TEST(test_read_until);
	RUN_TEST(test_read_until_and_close);
	RUN_TEST(test_read_until_not_found);
	RUN_TEST(test_read_until_zero_length_delim);
	RUN_TEST(test_read_until_NULL_delim);
	RUN_TEST(test_read_until_no_buffer);
	RUN_TEST(test_read_until_no_buffered_stream);
	RUN_TEST(test_read_until_no_handler);
	RUN_TEST(test_read_until_second_read_in_callback);
	RUN_TEST(test_read_at_least_then_until);

	RUN_TEST(test_write_one_buffer_one_chunk);
	RUN_TEST(test_write_one_buffer_one_chunk_read_in_callbacks_then_close);
	RUN_TEST(test_write_two_buffers_one_chunk);
	RUN_TEST(test_write_two_buffers_one_chunk_last_buffer_empty);
	RUN_TEST(test_write_two_buffers_partial_write);
	RUN_TEST(test_write_three_buffers_two_partial_writes);

	RUN_TEST(test_write_two_buffers_double_partial_write);
	RUN_TEST(test_write_two_buffers_partial_write_at_buffer_boundary);
	RUN_TEST(test_write_one_buffer_one_chunk_error);
	RUN_TEST(test_write_one_buffer_one_chunk_error_sync);
	RUN_TEST(test_write_one_buffer_partial_write_error);
	RUN_TEST(test_write_one_buffer_partial_write_error_at_element_boundary);
	RUN_TEST(test_write_one_buffer_partial_write_error_sync);
	RUN_TEST(test_write_one_buffer_partial_write_third_write_error_sync);
	RUN_TEST(test_write_no_buffered_stream_for_write);
	RUN_TEST(test_write_no_buffer_for_write);
	RUN_TEST(test_write_no_handler_for_write);
	RUN_TEST(test_close_no_stream);
	return UNITY_END();
}
