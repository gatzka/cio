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

#include "cio_error_code.h"
#include "cio_socket_address.h"
#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

void setUp(void)
{
	FFF_RESET_HISTORY()
}

void tearDown(void)
{
}

static void test_init_inet_socket_address(void)
{
	uint8_t ipv4_address[4] = {172, 19, 1, 1};
	struct cio_inet_address address;
	enum cio_error err = cio_init_inet_address(&address, ipv4_address, sizeof(ipv4_address));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "initialization of IPv4 inet address failed!");

	struct cio_socket_address sock_addr;
	err = cio_init_inet_socket_address(&sock_addr, &address, 12);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "initialization of inet socket address failed!");
}

static void test_init_inet_socket_address_no_sock_addr(void)
{
	uint8_t ipv4_address[4] = {172, 19, 1, 1};
	struct cio_inet_address address;
	enum cio_error err = cio_init_inet_address(&address, ipv4_address, sizeof(ipv4_address));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "initialization of IPv4 inet address failed!");

	err = cio_init_inet_socket_address(NULL, &address, 12);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "initialization of inet socket address didn't fail when no socket address object given!");
}

static void test_init_inet_socket_address_no_inet_address(void)
{
	struct cio_socket_address sock_addr;
	enum cio_error err = cio_init_inet_socket_address(&sock_addr, NULL, 12);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "initialization of inet socket address didn't fail when no inet address object given!");
}

static void test_init_inet_socket_address_wrong_family(void)
{
	uint8_t ipv4_address[4] = {172, 19, 1, 1};
	struct cio_inet_address address;
	enum cio_error err = cio_init_inet_address(&address, ipv4_address, sizeof(ipv4_address));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "initialization of IPv4 inet address failed!");

	address.impl.family = CIO_ADDRESS_FAMILY_UNSPEC;
	struct cio_socket_address sock_addr;
	err = cio_init_inet_socket_address(&sock_addr, &address, 12);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_FAMILY_NOT_SUPPORTED, err, "initialization of inet socket address did not fail with wrong address family!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_inet_socket_address);
	RUN_TEST(test_init_inet_socket_address_no_sock_addr);
	RUN_TEST(test_init_inet_socket_address_no_inet_address);
	RUN_TEST(test_init_inet_socket_address_wrong_family);
	return UNITY_END();
}
