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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "cio/utf8_checker.h"

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

#undef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct test_entry {
	const uint8_t *test_pattern;
	uint8_t valid;
};

void setUp(void)
{
	FFF_RESET_HISTORY();
}

void tearDown(void)
{
}

static void test_utf8(void)
{
	struct test_entry entries[] = {
	    {.test_pattern = (const uint8_t *)"\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87\x00", .valid = CIO_UTF8_ACCEPT},
	    {.test_pattern = (const uint8_t *)"Hello\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT},
	    {.test_pattern = (const uint8_t *)"Hello12\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT},
	    {.test_pattern = (const uint8_t *)"Hello123\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT},
	    {.test_pattern = (const uint8_t *)"Hello1234\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT},
	    {.test_pattern = (const uint8_t *)"Hello12345\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT},
	    {.test_pattern = (const uint8_t *)"Hello123456\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT},
	    {.test_pattern = (const uint8_t *)"Hello1234567\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT},
	    {.test_pattern = (const uint8_t *)"Hello12345678\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT},
	    {.test_pattern = (const uint8_t *)"H", .valid = CIO_UTF8_ACCEPT},
	    {.test_pattern = (const uint8_t *)"\xf8\x88\x80\x80\x80", .valid = CIO_UTF8_REJECT}};

	for (unsigned int i = 0; i < ARRAY_SIZE(entries); i++) {
		struct cio_utf8_state state;
		cio_utf8_init(&state);
		struct test_entry entry = entries[i];
		uint8_t result = cio_check_utf8(&state, entry.test_pattern, strlen((const char *)entry.test_pattern));
		TEST_ASSERT_MESSAGE(result == entry.valid, "Test pattern not checked successfully!");
	}
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_utf8);
	return UNITY_END();
}
