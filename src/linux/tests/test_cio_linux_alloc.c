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

#include "fff.h"
#include "unity.h"

#include "cio_allocator.h"

DEFINE_FFF_GLOBALS

void setUp(void)
{
	FFF_RESET_HISTORY();
}

static void test_allocator(void)
{
	struct cio_allocator *a = cio_get_system_allocator();
	TEST_ASSERT_NOT_EQUAL_MESSAGE(NULL, a, "cio_get_system_allocator did not deliver a valid allocator!");

	static const size_t buffer_size = 13;
	struct cio_buffer b = a->alloc(NULL, buffer_size);
	TEST_ASSERT_MESSAGE(b.size >= buffer_size, "Allocated buffer too small!");
	a->free(NULL, b.address);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_allocator);
	return UNITY_END();
}
