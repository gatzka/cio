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
#include <stdlib.h>

#include "cio/cio_base64.h"
#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

#undef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

void setUp(void)
{
	FFF_RESET_HISTORY();
}

void tearDown(void)
{
}

struct entry {
	const char *s;
	const char *base64_string;
};

static size_t base64_encoded_string_length(size_t input_length)
{
	return 4 * ((input_length + 2) / 3);
}

static void test_patterns(void)
{
	struct entry entries[] = {
	    {.s = "", .base64_string = ""},
	    {.s = "f", .base64_string = "Zg=="},
	    {.s = "fo", .base64_string = "Zm8="},
	    {.s = "foo", .base64_string = "Zm9v"},
	    {.s = "foob", .base64_string = "Zm9vYg=="},
	    {.s = "fooba", .base64_string = "Zm9vYmE="},
	    {.s = "foobar", .base64_string = "Zm9vYmFy"},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(entries); i++) {
		struct entry e = entries[i];
		size_t len = strlen(e.s);
		size_t result_buffer_length = base64_encoded_string_length(len);
		char *out = malloc(result_buffer_length);
		cio_b64_encode_buffer((const uint8_t *)e.s, len, out);

		TEST_ASSERT_EQUAL_MESSAGE(strlen(e.base64_string), result_buffer_length, "length of base64 output buffer was not calculated correctly");
		if (result_buffer_length > 0) {
			TEST_ASSERT_EQUAL_MEMORY_MESSAGE(e.base64_string, out, result_buffer_length, "base64 conversion not correct!");
		} else {
			TEST_ASSERT_EQUAL_MESSAGE(0, len, "result buffer length is not 0 if input length is 0");
		}

		free(out);
	}
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_patterns);
	return UNITY_END();
}
