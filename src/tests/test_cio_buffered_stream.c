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

#include <stdlib.h>
#include <string.h>

#include "fff.h"
#include "unity.h"
#include "cio_allocator.h"
#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_io_stream.h"

DEFINE_FFF_GLOBALS

#undef container_of
#define container_of(ptr, type, member) ( \
	(void *)((char *)ptr - offsetof(type, member)))

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

struct memory_stream {
	struct cio_io_stream ios;
	void *mem;
};

static uint8_t check_buffer[100];
static size_t check_buffer_pos = 0;

void read_some(struct cio_io_stream *ios, void *buf, size_t num, cio_io_stream_read_handler handler, void *context);
FAKE_VOID_FUNC(read_some, struct cio_io_stream*, void*, size_t, cio_io_stream_read_handler, void*)

void close(struct cio_io_stream *context);
FAKE_VOID_FUNC(close, struct cio_io_stream*)

void dummy_handler(struct cio_buffered_stream*, void*, enum cio_error, uint8_t*, size_t);
FAKE_VOID_FUNC(dummy_handler, struct cio_buffered_stream*, void*, enum cio_error, uint8_t*, size_t)

static void save_to_check_buffer(struct cio_buffered_stream *bs, void *context, enum cio_error err, uint8_t *buffer, size_t num) {
	(void)bs;
	(void)context;
	if (err == cio_success) {
		memcpy(&check_buffer[check_buffer_pos], buffer, num);
		check_buffer_pos += num;
	}
}

static struct cio_buffer alloc_no_mem(struct cio_allocator *context, size_t size)
{
	(void)context;
	(void)size;
	struct cio_buffer buffer;
	buffer.address = NULL;
	buffer.size = 0;
	return buffer;
}

static void read_some_max(struct cio_io_stream *ios, void *buf, size_t num, cio_io_stream_read_handler handler, void *context)
{
	struct memory_stream *ms = container_of(ios, struct memory_stream, ios);
	size_t len = MIN(num, strlen(ms->mem) + 1);
	memcpy(buf, ms->mem, len);
	handler(ios, context, cio_success, buf, len);
}

static void read_some_error(struct cio_io_stream *ios, void *buf, size_t num, cio_io_stream_read_handler handler, void *context)
{
	handler(ios, context, cio_invalid_argument, buf, num);
}

static void read_some_chunks(struct cio_io_stream *ios, void *buf, size_t num, cio_io_stream_read_handler handler, void *context)
{
	struct memory_stream *ms = container_of(ios, struct memory_stream, ios);
	size_t string_len = strlen(ms->mem);
	if (read_some_fake.call_count == 1) {
		string_len = string_len / 2;
		size_t len = MIN(num, string_len);
		memcpy(buf, ms->mem, len);
		handler(ios, context, cio_success, buf, len);
	} else {
		size_t pos = string_len / 2;
		string_len = string_len - pos;
		size_t len = MIN(num, string_len);
		memcpy(buf, (uint8_t *)ms->mem + pos, len);
		handler(ios, context, cio_success, buf, len);
	}
}

static void memory_stream_init(struct memory_stream *ms, const char *fill_pattern)
{
	ms->ios.read_some = read_some;
	ms->ios.close = close;
	ms->mem = malloc(strlen(fill_pattern) + 1);
	memset(ms->mem, 0x00, strlen(fill_pattern) + 1);
	strncpy(ms->mem, fill_pattern, strlen(fill_pattern));
}

static void memory_stream_deinit(struct memory_stream *ms)
{
	free(ms->mem);
}

static struct cio_allocator allocator_no_mem = {
	.alloc = alloc_no_mem,
	.free = NULL
};

static struct memory_stream ms;

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(read_some);
	RESET_FAKE(close);
	RESET_FAKE(dummy_handler);

	memset(check_buffer, 0xaf, sizeof(check_buffer));
	check_buffer_pos = 0;
}

static void test_init_missing_stream(void)
{
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, NULL, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL(cio_invalid_argument, err);
}

static void test_init_missing_read_allocator(void)
{
	struct cio_io_stream ios;
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ios, 40, NULL, 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL(cio_invalid_argument, err);
}

static void test_init_missing_write_allocator(void)
{
	struct cio_io_stream ios;
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ios, 40, cio_get_system_allocator(), 30, NULL);
	TEST_ASSERT_EQUAL(cio_invalid_argument, err);
}

static void test_init_correctly(void)
{
	memory_stream_init(&ms, "hello");
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL(cio_success, err);
	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	memory_stream_deinit(&ms);
}

static void test_init_alloc_read_fails(void)
{
	struct cio_io_stream ios;
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ios, 40, &allocator_no_mem, 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL(cio_not_enough_memory, err);
}

static void test_init_alloc_write_fails(void)
{
	struct cio_io_stream ios;
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ios, 40, cio_get_system_allocator(), 30, &allocator_no_mem);
	TEST_ASSERT_EQUAL(cio_not_enough_memory, err);
}

static void test_read_exactly(void)
{
	static const char *test_data = "Hello";
	memory_stream_init(&ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_handler_fake.custom_fake = save_to_check_buffer;

	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");
	bs.read_exactly(&bs, strlen(test_data), dummy_handler, check_buffer);

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, dummy_handler_fake.arg2_val, "Handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(strlen(test_data), dummy_handler_fake.arg4_val, "Handler was not called with correct data length!");
	TEST_ASSERT_MESSAGE(strncmp((const char *)check_buffer, test_data, strlen(test_data)) == 0, "Handler was not called with correct data!")
	memory_stream_deinit(&ms);
}

static void test_read_exactly_zero_length(void)
{
	static const char *test_data = "Hello";
	memory_stream_init(&ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_handler_fake.custom_fake = save_to_check_buffer;

	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");
	bs.read_exactly(&bs, 0, dummy_handler, check_buffer);

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, dummy_handler_fake.arg2_val, "Handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_handler_fake.arg4_val, "Handler was not called with correct data length!");
	memory_stream_deinit(&ms);
}

static void test_read_exactly_more_than_buffer_size(void)
{
	size_t read_buffer_size = 40;
	static const char *test_data = "Hello";
	memory_stream_init(&ms, test_data);

	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, read_buffer_size, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");
	bs.read_exactly(&bs, read_buffer_size + 1, dummy_handler, NULL);

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_message_too_long, dummy_handler_fake.arg2_val, "Handler was not called with cio_success!");
	memory_stream_deinit(&ms);
}

static void test_read_exactly_ios_error(void)
{
	size_t read_buffer_size = 40;
	static const char *test_data = "Hello";
	memory_stream_init(&ms, test_data);
	read_some_fake.custom_fake = read_some_error;

	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, read_buffer_size, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");
	bs.read_exactly(&bs, read_buffer_size - 1, dummy_handler, NULL);

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, dummy_handler_fake.arg2_val, "Handler was not called with cio_invalid_argument!");
	memory_stream_deinit(&ms);
}

static void test_read_exactly_chunks(void)
{
	size_t read_buffer_size = 40;
	static const char *test_data = "HelloWorld!";
	memory_stream_init(&ms, test_data);
	read_some_fake.custom_fake = read_some_chunks;
	dummy_handler_fake.custom_fake = save_to_check_buffer;

	size_t first_chunk = 2;
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, read_buffer_size, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");
	bs.read_exactly(&bs, first_chunk, dummy_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, dummy_handler_fake.arg2_history[0], "Handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(first_chunk, dummy_handler_fake.arg4_history[0], "Length in dummy handler is not correct!");

	size_t second_chunk = strlen(test_data) - first_chunk;
	bs.read_exactly(&bs, second_chunk, dummy_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(2, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, dummy_handler_fake.arg2_history[1], "Handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(second_chunk, dummy_handler_fake.arg4_history[1], "Length in dummy handler is not correct!");

	TEST_ASSERT_MESSAGE(strncmp((const char *)check_buffer, test_data, strlen(test_data)) == 0, "Handler was not called with correct data!")

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	memory_stream_deinit(&ms);
}

static void test_read_until(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_handler_fake.custom_fake = save_to_check_buffer;

	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");

	bs.read_until(&bs, DELIM, dummy_handler, check_buffer);

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, dummy_handler_fake.arg2_val, "Handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(strlen(PRE_DELIM) + strlen(DELIM), dummy_handler_fake.arg4_val, "Handler was not called with correct data length!");
	TEST_ASSERT_MESSAGE(strncmp((const char *)check_buffer, PRE_DELIM DELIM, strlen(PRE_DELIM) + strlen(DELIM)) == 0, "Handler was not called with correct data!")
	memory_stream_deinit(&ms);
}

static void test_read_until_zero_length_delim(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_handler_fake.custom_fake = save_to_check_buffer;

	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");

	bs.read_until(&bs, "", dummy_handler, check_buffer);

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, dummy_handler_fake.arg2_val, "Handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(0, dummy_handler_fake.arg4_val, "Handler was not called with correct data length!");
	memory_stream_deinit(&ms);
}

static void test_read_until_NULL_delim(void)
{
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
	static const char *test_data = PRE_DELIM DELIM "Example";
	memory_stream_init(&ms, test_data);
	read_some_fake.custom_fake = read_some_max;
	dummy_handler_fake.custom_fake = save_to_check_buffer;

	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");

	bs.read_until(&bs, NULL, dummy_handler, check_buffer);

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, dummy_handler_fake.arg2_val, "Handler was not called with cio_invalid_argument!");
	memory_stream_deinit(&ms);
}

static void test_read_exactly_then_until(void)
{
#define BUFFER_FOR_EXACTLY "hhhhhhhhhhhhh"
#define PRE_DELIM "MY"
#define DELIM "HelloWorld"
#define REST "Example"
	static const char *test_data = BUFFER_FOR_EXACTLY PRE_DELIM DELIM REST;
	memory_stream_init(&ms, test_data);
	read_some_fake.custom_fake = read_some_chunks;
	dummy_handler_fake.custom_fake = save_to_check_buffer;

	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &ms.ios, strlen(BUFFER_FOR_EXACTLY PRE_DELIM DELIM) - 2, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Buffer was not initialized correctly!");

	bs.read_exactly(&bs, strlen(BUFFER_FOR_EXACTLY), dummy_handler, check_buffer);
	bs.read_until(&bs, DELIM, dummy_handler, check_buffer);

	bs.close(&bs);
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Underlying cio_iostream was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(2, dummy_handler_fake.call_count, "Handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, dummy_handler_fake.arg2_history[0], "Handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(strlen(BUFFER_FOR_EXACTLY), dummy_handler_fake.arg4_history[0], "Length in dummy handler is not correct!");
	TEST_ASSERT_MESSAGE(strncmp((const char *)check_buffer, BUFFER_FOR_EXACTLY, strlen(BUFFER_FOR_EXACTLY)) == 0, "Handler was not called with correct data!")

	TEST_ASSERT_EQUAL_MESSAGE(cio_success, dummy_handler_fake.arg2_history[1], "Handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(strlen(PRE_DELIM DELIM), dummy_handler_fake.arg4_history[1], "Length in dummy handler is not correct!");
	TEST_ASSERT_MESSAGE(strncmp((const char *)&check_buffer[strlen(BUFFER_FOR_EXACTLY)], PRE_DELIM DELIM, strlen(PRE_DELIM) + strlen(DELIM)) == 0, "Handler was not called with correct data!")

	memory_stream_deinit(&ms);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_missing_stream);
	RUN_TEST(test_init_missing_read_allocator);
	RUN_TEST(test_init_missing_write_allocator);
	RUN_TEST(test_init_correctly);
	RUN_TEST(test_init_alloc_read_fails);
	RUN_TEST(test_init_alloc_write_fails);
	RUN_TEST(test_read_exactly);
	RUN_TEST(test_read_exactly_more_than_buffer_size);
	RUN_TEST(test_read_exactly_ios_error);
	RUN_TEST(test_read_exactly_chunks);
	RUN_TEST(test_read_exactly_zero_length);
	RUN_TEST(test_read_until);
	RUN_TEST(test_read_until_zero_length_delim);
	RUN_TEST(test_read_until_NULL_delim);
	RUN_TEST(test_read_exactly_then_until);
	return UNITY_END();
}
