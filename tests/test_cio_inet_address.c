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
#include "cio_inet_address.h"
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

static void test_init_inet_v4_address(void)
{
	uint8_t ipv4_address[4] = {172, 19, 1, 1};
	struct cio_inet_address address;
	enum cio_error err = cio_init_inet_address(&address, ipv4_address, sizeof(ipv4_address));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "initialization of IPv4 inet address failed!");
}

static void test_init_inet_v6_address(void)
{
	uint8_t ipv6_address[16] = {0};
	struct cio_inet_address address;
	enum cio_error err = cio_init_inet_address(&address, ipv6_address, sizeof(ipv6_address));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "initialization of IPv6 inet address failed!");
}

static void test_init_inet_address_no_address(void)
{
	uint8_t ipv6_address[16] = {0};
	enum cio_error err = cio_init_inet_address(NULL, ipv6_address, sizeof(ipv6_address));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "cio_init_inet_address didn't fail if no address given!");
}

static void test_init_inet_address_no_ipaddress(void)
{
	uint8_t ipv6_address[16] = {0};
	struct cio_inet_address address;
	enum cio_error err = cio_init_inet_address(&address, NULL, sizeof(ipv6_address));
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "cio_init_inet_address didn't fail if no IP address given!");
}

static void test_init_inet_address_wrong_ipaddress_size(void)
{
	uint8_t ipv6_address[16] = {0};
	struct cio_inet_address address;
	enum cio_error err = cio_init_inet_address(&address, ipv6_address, sizeof(ipv6_address) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "cio_init_inet_address didn't fail if size ofIP address wrong!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_inet_v4_address);
	RUN_TEST(test_init_inet_v6_address);
	RUN_TEST(test_init_inet_address_no_address);
	RUN_TEST(test_init_inet_address_no_ipaddress);
	RUN_TEST(test_init_inet_address_wrong_ipaddress_size);
	return UNITY_END();
}
