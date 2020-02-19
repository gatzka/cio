/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2019> <Stephan Gatzka>
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

#include "cio_error_code.h"
#include "cio_random.h"
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

static void test_two_randoms(void)
{
	cio_rng rng;

	enum cio_error err = cio_random_seed_rng(&rng);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "cio_random_seed_rng did not succeed!");

	uint64_t first_rand;
	uint64_t second_rand;

	cio_random_get_bytes(&rng, &first_rand, sizeof(first_rand));
	cio_random_get_bytes(&rng, &second_rand, sizeof(second_rand));

	int equal = memcmp(&first_rand, &second_rand, sizeof(first_rand));
	TEST_ASSERT_NOT_EQUAL_MESSAGE(0, equal, "Two calls for random lead to the same result!")
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_two_randoms);
	return UNITY_END();
}
