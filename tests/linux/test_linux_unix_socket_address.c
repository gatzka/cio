/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2020> <Stephan Gatzka>
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

#include "fff.h"
#include "unity.h"

#include "cio/error_code.h"
#include "cio/socket_address.h"
#include "cio/unix_address.h"

DEFINE_FFF_GLOBALS

void setUp(void)
{
	FFF_RESET_HISTORY()
}

void tearDown(void)
{
}

static void test_init_uds_socket_address(void)
{
	struct cio_socket_address address;
	enum cio_error err = cio_init_uds_socket_address(&address, "foobar");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "initialization of UDS socket address failed!");
}

static void test_init_abstract_uds_socket_address(void)
{
	struct cio_socket_address address;
	enum cio_error err = cio_init_uds_socket_address(&address, "\0foobar");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "initialization of an abstract UDS socket address failed!");
}

static void test_init_uds_socket_address_no_sock_addr(void)
{
	enum cio_error err = cio_init_uds_socket_address(NULL, "foobar");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "initialization of uds socket address didn't fail when no socket address object given!");
}

static void test_init_uds_socket_address_no_path(void)
{
	struct cio_socket_address address;
	enum cio_error err = cio_init_uds_socket_address(&address, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "initialization of uds socket address didn't fail when no path given!");
}

static void test_init_uds_socket_address_path_too_long(void)
{
	char path[1000];
	memset(path, 'h', sizeof(path));
	path[1000 - 1] = '\0';
	struct cio_socket_address address;
	enum cio_error err = cio_init_uds_socket_address(&address, path);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "initialization of uds socket address didn't fail when path is too long");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_uds_socket_address);
	RUN_TEST(test_init_uds_socket_address_no_sock_addr);
	RUN_TEST(test_init_uds_socket_address_no_path);
	RUN_TEST(test_init_uds_socket_address_path_too_long);
	RUN_TEST(test_init_abstract_uds_socket_address);

	return UNITY_END();
}
