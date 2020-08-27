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

#include <stdint.h>

#include "cio/endian.h"

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

static void test_convert_be16(void)
{
	uint8_t pattern_array[] = {0x34, 0x12};
	uint16_t pattern;
	memcpy(&pattern, pattern_array, sizeof(pattern));
	uint16_t check_pattern = 0x3412;

	uint16_t check = cio_be16toh(pattern);
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&check_pattern, &check, sizeof(check), "16 bit endian conversion incorrect!");
}

static void test_convert_htobe16(void)
{
	uint8_t pattern_array[] = {0x12, 0x34};
	uint16_t pattern;
	memcpy(&pattern, pattern_array, sizeof(pattern));
	uint16_t check_pattern = 0x1234;

	uint16_t check = cio_htobe16(pattern);

	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&check_pattern, &check, sizeof(check), "16 bit endian conversion incorrect!");
}

static void test_convert_be32(void)
{
	uint8_t pattern_array[] = {0x12, 0x34, 0x56, 0x78};
	uint32_t pattern;
	memcpy(&pattern, pattern_array, sizeof(pattern));
	uint32_t check_pattern = 0x12345678;

	uint32_t check = cio_be32toh(pattern);
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&check_pattern, &check, sizeof(check), "32 bit endian conversion incorrect!");
}

static void test_convert_htobe32(void)
{
	uint8_t pattern_array[] = {0x78, 0x56, 0x34, 0x12};
	uint32_t pattern;
	memcpy(&pattern, pattern_array, sizeof(pattern));
	uint32_t check_pattern = 0x78563412;

	uint32_t check = cio_htobe32(pattern);

	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&check_pattern, &check, sizeof(check), "32 bit endian conversion incorrect!");
}


static void test_convert_be64(void)
{
	uint8_t pattern_array[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
	uint64_t pattern;
	memcpy(&pattern, pattern_array, sizeof(pattern));
	uint64_t check_pattern = 0x123456789abcdef0;
	uint64_t check = cio_be64toh(pattern);

	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&check_pattern, &check, sizeof(check), "64 bit endian conversion incorrect!");
}

static void test_convert_htobe64(void)
{
	uint8_t pattern_array[] = {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12};
	uint64_t pattern;
	memcpy(&pattern, pattern_array, sizeof(pattern));
	uint64_t check_pattern = 0xf0debc9a78563412;

	uint64_t check = cio_htobe64(pattern);
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&check_pattern, &check, sizeof(check), "64 bit endian conversion incorrect!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_convert_be16);
	RUN_TEST(test_convert_htobe16);
	RUN_TEST(test_convert_be32);
	RUN_TEST(test_convert_htobe32);
	RUN_TEST(test_convert_be64);
	RUN_TEST(test_convert_htobe64);
	return UNITY_END();
}
