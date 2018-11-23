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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "cio_random.h"
#include "cio_websocket_masking.h"

#include "fff.h"
#include "unity.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

DEFINE_FFF_GLOBALS

void setUp(void)
{
	FFF_RESET_HISTORY();
}

void tearDown(void)
{
}

static void fill_random(uint8_t *buffer, size_t length)
{
	cio_random_get_bytes(buffer, length);
}

static void check_masking(uint8_t *buffer, size_t length, const uint8_t mask[4])
{
	for (size_t i = 0; i < length; i++) {
		buffer[i] = buffer[i] ^ (mask[i % 4]);
	}
}

static void test_aligned_buffer(void)
{
#define buffer_size 16
#define max_align 8

	for (unsigned int align_counter = 0; align_counter < max_align; align_counter++) {
		for (unsigned int length_counter = 1; length_counter < buffer_size; length_counter++) {
			uint8_t buffer[buffer_size + max_align];
			fill_random(buffer, sizeof(buffer));

			uint8_t check_buffer[buffer_size + max_align];
			memcpy(check_buffer, buffer, sizeof(buffer));

			uint8_t mask[4];
			fill_random(mask, sizeof(mask));

			cio_websocket_mask(buffer + align_counter, length_counter, mask);
			check_masking(check_buffer + align_counter, length_counter, mask);

			TEST_ASSERT_MESSAGE(memcmp(check_buffer + align_counter, buffer + align_counter, length_counter) == 0, "Message masking not correct!");
		}
	}
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_aligned_buffer);
	return UNITY_END();
}
