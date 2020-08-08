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

#include "cio/cio_version.h"
#include "cio/cio_version_private.h"

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

static void test_get_version_string(void)
{
	const char *version = cio_get_version_string();
	TEST_ASSERT_NOT_NULL_MESSAGE(version, "No version string delivered");
	TEST_ASSERT_EQUAL_STRING_MESSAGE(CIO_VERSION, version, "Version string not correct!");
}

static void test_get_version_major(void)
{
	TEST_ASSERT_EQUAL_MESSAGE(CIO_VERSION_MAJOR, cio_get_version_major(), "Major version not correct!");
}

static void test_get_version_minor(void)
{
	TEST_ASSERT_EQUAL_MESSAGE(CIO_VERSION_MINOR, cio_get_version_minor(), "Minor version not correct!");
}

static void test_get_version_patch(void)
{
	TEST_ASSERT_EQUAL_MESSAGE(CIO_VERSION_PATCH, cio_get_version_patch(), "Patch version not correct!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_get_version_string);
	RUN_TEST(test_get_version_major);
	RUN_TEST(test_get_version_minor);
	RUN_TEST(test_get_version_patch);
	return UNITY_END();
}
