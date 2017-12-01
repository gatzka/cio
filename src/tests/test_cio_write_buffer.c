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

#include "cio_error_code.h"
#include "cio_write_buffer.h"
#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

void setUp(void)
{
	FFF_RESET_HISTORY();
}

static void test_peek_from_empty_queue(void)
{
	struct cio_write_buffer wb;
	cio_write_buffer_head_init(&wb);

	struct cio_write_buffer *element = cio_write_buffer_queue_dequeue(&wb);
	TEST_ASSERT_EQUAL_MESSAGE(NULL, element, "Dequeueing from empty queue did not return NULL!");
}

static void test_splice_empty_list(void)
{
	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, NULL, 0);
	cio_write_buffer_queue_tail(&wbh, &wb);
	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_number_of_elements(&wbh), "Number of elements in write buffer not correct!");

	struct cio_write_buffer list_to_append;
	cio_write_buffer_head_init(&list_to_append);
	cio_write_buffer_splice(&list_to_append, &wbh);
	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_number_of_elements(&wbh), "Number of elements in write buffer not correct!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_peek_from_empty_queue);
	RUN_TEST(test_splice_empty_list);
	return UNITY_END();
}
