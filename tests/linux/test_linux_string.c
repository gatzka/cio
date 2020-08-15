/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2018> <Stephan Gatzka>
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

#include <stddef.h>
#include <stdint.h>

#include "cio/string.h"

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

static void test_cio_memmem(void)
{
	const char haystack[] = "abcd";
	const char needle_found[] = "a";

	const void *found = cio_memmem(haystack, sizeof(haystack), needle_found, strlen(needle_found));
	TEST_ASSERT_EQUAL_MESSAGE(found, haystack, "Needle was not found in haystack!");

	char needle_not_found[] = "e";
	found = cio_memmem(haystack, sizeof(haystack), needle_not_found, strlen(needle_not_found));
	TEST_ASSERT_EQUAL_MESSAGE(NULL, found, "Needle was found in haystack!");
}

static void test_cio_strncasecmp(void)
{
	const char *s1 = "Hello World!";
	const char *s2 = "HELLO";
	int ret = cio_strncasecmp(s1, s2, strlen(s2));
	TEST_ASSERT_EQUAL_MESSAGE(0, ret, "String s2 not found in s1 after ignoring case!");

	const char *s3 = "Hello";
	ret = cio_strncasecmp(s1, s3, strlen(s3));
	TEST_ASSERT_EQUAL_MESSAGE(0, ret, "String s3 not found in s1 after ignoring case!");

	const char *s4 = "World";
	ret = cio_strncasecmp(s1, s4, strlen(s4));
	TEST_ASSERT_NOT_EQUAL_MESSAGE(0, ret, "String s4 found in s1 after ignoring case!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_cio_memmem);
	RUN_TEST(test_cio_strncasecmp);
	return UNITY_END();
}
