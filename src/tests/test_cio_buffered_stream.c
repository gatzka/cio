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

struct memory_stream {
	struct cio_io_stream ios;
	void *mem;
};

void read_some(struct cio_io_stream *ios, void *buf, size_t num, cio_io_stream_read_handler handler, void *context);
FAKE_VOID_FUNC(read_some, struct cio_io_stream*, void*, size_t, cio_io_stream_read_handler, void*)

void close(struct cio_io_stream *context);
FAKE_VOID_FUNC(close, struct cio_io_stream*)

static struct cio_buffer alloc_no_mem(struct cio_allocator *context, size_t size)
{
	(void)context;
	(void)size;
	struct cio_buffer buffer;
	buffer.address = NULL;
	buffer.size = 0;
	return buffer;
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

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_missing_stream);
	RUN_TEST(test_init_missing_read_allocator);
	RUN_TEST(test_init_missing_write_allocator);
	RUN_TEST(test_init_correctly);
	RUN_TEST(test_init_alloc_read_fails);
	RUN_TEST(test_init_alloc_write_fails);
	return UNITY_END();
}
