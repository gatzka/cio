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

#include <errno.h>
#include <stddef.h>
#include <stdio.h>

#include "fff.h"
#include "unity.h"

#include "cio/error_code.h"
#include "cio/random.h"

DEFINE_FFF_GLOBALS

#undef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

FAKE_VALUE_FUNC(FILE *, fopen, const char *, const char *)
FAKE_VALUE_FUNC(int, getc_unlocked, FILE *)
FAKE_VALUE_FUNC(int, fclose, FILE *)

void setUp(void)
{
	FFF_RESET_HISTORY()

	RESET_FAKE(fopen)
	RESET_FAKE(getc_unlocked)
	RESET_FAKE(fclose)
}

void tearDown(void)
{
}

static void test_random_seed_fails(void)
{
	cio_rng_t rng;
	fopen_fake.return_val = NULL;
	errno = EACCES;

	enum cio_error err = cio_random_seed_rng(&rng);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_PERMISSION_DENIED, err, "cio_random_seed_rng did not fail with correct return value");
}

static void test_fread_short(void)
{
	cio_rng_t rng;
	fopen_fake.return_val = (FILE *)8;
	int ret_vals[3] = { 3, 7, EOF };
	SET_RETURN_SEQ(getc_unlocked, ret_vals, ARRAY_SIZE(ret_vals));
	enum cio_error err = cio_random_seed_rng(&rng);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EOF, err, "cio_random_seed_rng did not fail on short freads");
}

static void test_fread_ok(void)
{
	cio_rng_t rng;
	fopen_fake.return_val = (FILE *)8;
	getc_unlocked_fake.return_val = 0xaa;
	enum cio_error err = cio_random_seed_rng(&rng);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "cio_random_seed_rng did failed");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_random_seed_fails);
	RUN_TEST(test_fread_short);
	RUN_TEST(test_fread_ok);
	return UNITY_END();
}
