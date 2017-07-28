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


#include "fff.h"
#include "unity.h"
#include "cio_allocator.h"
#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_io_stream.h"

DEFINE_FFF_GLOBALS

static struct cio_buffer alloc_no_mem(struct cio_allocator *context, size_t size)
{
	(void)context;
	(void)size;
	struct cio_buffer buffer;
	buffer.address = NULL;
	buffer.size= 0;
	return buffer;
}


static struct cio_allocator allocator_no_mem = {
	.alloc = alloc_no_mem,
	.free = NULL
};

static struct cio_io_stream dummy_stream;

static void test_init_missing_stream(void)
{
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, NULL, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL(cio_invalid_argument, err);
}

static void test_init_missing_read_allocator(void)
{
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &dummy_stream, 40, NULL, 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL(cio_invalid_argument, err);
}

static void test_init_missing_write_allocator(void)
{
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &dummy_stream, 40, cio_get_system_allocator(), 30, NULL);
	TEST_ASSERT_EQUAL(cio_invalid_argument, err);
}

static void test_init_correctly(void)
{
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &dummy_stream, 40, cio_get_system_allocator(), 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL(cio_success, err);
	bs.close(&bs);
}

static void test_init_alloc_read_fails(void)
{
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &dummy_stream, 40, &allocator_no_mem, 30, cio_get_system_allocator());
	TEST_ASSERT_EQUAL(cio_not_enough_memory, err);
}

static void test_init_alloc_write_fails(void)
{
	struct cio_buffered_stream bs;
	enum cio_error err = cio_buffered_stream_init(&bs, &dummy_stream, 40, cio_get_system_allocator(), 30, &allocator_no_mem);
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
