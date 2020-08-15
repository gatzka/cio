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

#include <stdint.h>

#include "cio/error_code.h"
#include "cio/read_buffer.h"

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

void setUp(void)
{
	FFF_RESET_HISTORY();
}

void tearDown(void)
{
}

static void test_init_read_buffer(void)
{
	uint32_t buffer;
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Read buffer was not initialized correctly!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(buffer), cio_read_buffer_space_available(&rb), "Available space not initialized correctly!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_read_buffer_unread_bytes(&rb), "Unread bytes was not initialized correctly!");

	TEST_ASSERT_EQUAL_MESSAGE(sizeof(buffer), cio_read_buffer_size(&rb), "Size of read buffer not initialized correctly!");
}

static void test_init_no_read_buffer(void)
{
	uint32_t buffer;
	enum cio_error err = cio_read_buffer_init(NULL, &buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value for initialization with no read buffer not correct!");
}

static void test_init_no_buffer(void)
{
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, NULL, 4);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value for initialization with no buffer provided is not correct!");
}

static void test_init_no_buffer_size_zero(void)
{
	uint32_t buffer;
	struct cio_read_buffer rb;
	enum cio_error err = cio_read_buffer_init(&rb, &buffer, 0);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value for initialization with buffer size \"0\" not correct!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_read_buffer);
	RUN_TEST(test_init_no_read_buffer);
	RUN_TEST(test_init_no_buffer);
	RUN_TEST(test_init_no_buffer_size_zero);
	return UNITY_END();
}
