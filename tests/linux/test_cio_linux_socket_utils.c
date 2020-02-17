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

#include <errno.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "fff.h"
#include "unity.h"

#include "cio_inet_address.h"
#include "cio_linux_socket_utils.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, socket, int, int, int)
FAKE_VALUE_FUNC(int, getsockopt, int, int, int, void *, socklen_t *)

static int getsockopt_ok(int fd, int level, int optname, void *optval, socklen_t *len)
{
	(void)fd;
	(void)level;
	(void)optname;
	(void)len;

	int err = ENOBUFS;
	*((int *)(optval)) = err;
	return 0;
}

static int getsockopt_error(int fd, int level, int optname, void *optval, socklen_t *len)
{
	(void)fd;
	(void)level;
	(void)optname;
	(void)len;
	(void)optval;
	errno = EINVAL;

	return -1;
}

void setUp(void)
{
	FFF_RESET_HISTORY()
	RESET_FAKE(socket)
	RESET_FAKE(getsockopt)
}

void tearDown(void)
{
}

static void test_create_inet4_socket(void)
{
	int socket_fd = 5;
	socket_fake.return_val = socket_fd;
	int ret = cio_linux_socket_create(CIO_SA_INET4_ADDRESS);

	TEST_ASSERT_EQUAL(ret, socket_fd);
	TEST_ASSERT_EQUAL(1, socket_fake.call_count);
}

static void test_create_inet6_socket(void)
{
	int socket_fd = 5;
	socket_fake.return_val = socket_fd;
	int ret = cio_linux_socket_create(CIO_SA_INET6_ADDRESS);

	TEST_ASSERT_EQUAL(ret, socket_fd);
	TEST_ASSERT_EQUAL(1, socket_fake.call_count);
}

static void test_create_socket_fails(void)
{
	socket_fake.return_val = -1;
	int ret = cio_linux_socket_create(CIO_SA_INET4_ADDRESS);

	TEST_ASSERT_EQUAL(-1, ret);
	TEST_ASSERT_EQUAL(1, socket_fake.call_count);
}

static void test_create_socket_wrong_family(void)
{
	int socket_fd = 5;
	socket_fake.return_val = socket_fd;
	int ret = cio_linux_socket_create((enum cio_socket_address_family)33);

	TEST_ASSERT_EQUAL_MESSAGE(-1, ret, "wrong return value if called with illegal address family!");
	TEST_ASSERT_EQUAL(0, socket_fake.call_count);
}

static void test_get_socket_error_success(void)
{
	getsockopt_fake.custom_fake = getsockopt_ok;
	enum cio_error err = cio_linux_get_socket_error(5);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_NO_BUFFER_SPACE, err, "Wrong error value!");
}

static void test_get_socket_error_failue(void)
{
	getsockopt_fake.custom_fake = getsockopt_error;
	enum cio_error err = cio_linux_get_socket_error(5);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Wrong error value!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_create_inet4_socket);
	RUN_TEST(test_create_inet6_socket);
	RUN_TEST(test_create_socket_fails);
	RUN_TEST(test_create_socket_wrong_family);
	RUN_TEST(test_get_socket_error_success);
	RUN_TEST(test_get_socket_error_failue);
	return UNITY_END();
}
