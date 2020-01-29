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

#include <stdarg.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "fff.h"
#include "unity.h"

#include "cio_linux_socket_utils.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, socket, int, int, int)
FAKE_VALUE_FUNC(int, close, int)

void setUp(void)
{
	FFF_RESET_HISTORY();
	RESET_FAKE(socket);

	RESET_FAKE(close);
}

void tearDown(void)
{
}

static void test_create_socket_no_fd(void)
{
	int socket_fd = 5;
	socket_fake.return_val = socket_fd;
	int ret = cio_linux_socket_create(AF_INET);

	TEST_ASSERT_EQUAL(ret, socket_fd);
	TEST_ASSERT_EQUAL(1, socket_fake.call_count);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
}

static void test_create_socket_fails(void)
{
	socket_fake.return_val = -1;
	int ret = cio_linux_socket_create(AF_INET);

	TEST_ASSERT_EQUAL(-1, ret);
	TEST_ASSERT_EQUAL(1, socket_fake.call_count);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_create_socket_no_fd);
	RUN_TEST(test_create_socket_fails);
	return UNITY_END();
}
