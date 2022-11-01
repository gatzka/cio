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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "cio/error_code.h"
#include "cio/write_buffer.h"

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

static void test_cio_write_buffer_element_init(void)
{
	unsigned int data = 0x12;
	struct cio_write_buffer wb;
	cio_write_buffer_element_init(&wb, &data, sizeof(data));
}

static void test_cio_write_buffer_const_element_init(void)
{
	const char data[] = "Hello World!";
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, data, sizeof(data));
}

static void test_cio_write_buffer_head_init(void)
{
	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	TEST_ASSERT_TRUE_MESSAGE(cio_write_buffer_queue_empty(&wbh), "Write buffer not empty after initialization of write_buffer_head!");
}

static void test_cio_write_buffer_queue_tail(void)
{
	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	size_t num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(0, num_elements, "Number of elements in write buffer not '0' after initialization of write buffer head!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	unsigned int data1 = 0x12;
	struct cio_write_buffer wb1;
	cio_write_buffer_element_init(&wb1, &data1, sizeof(data1));
	cio_write_buffer_queue_tail(&wbh, &wb1);
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(1, num_elements, "Number of elements in write buffer not '1' after inserting first element!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1), cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	const char data2[] = "Hello World!";
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, data2, sizeof(data2));
	cio_write_buffer_queue_tail(&wbh, &wb2);
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(2, num_elements, "Number of elements in write buffer not '2' after inserting second element!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1) + sizeof(data2), cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	struct cio_write_buffer *dequeued_wb = cio_write_buffer_queue_dequeue(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(&wb1, dequeued_wb, "First dequeued write buffer is not the write buffer inserted first!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(1, num_elements, "Number of elements in write buffer not '1' after dequeueing first element!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data2), cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	dequeued_wb = cio_write_buffer_queue_dequeue(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(&wb2, dequeued_wb, "First dequeued write buffer is not the write buffer inserted secondly!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(0, num_elements, "Number of elements in write buffer not '0' after dequeueing second element!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	dequeued_wb = cio_write_buffer_queue_dequeue(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(NULL, dequeued_wb, "Return value of cio_write_buffer_queue_dequeue not NULL when called on an empty write buffer!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(0, num_elements, "Number of elements in write buffer not '0' after dequeueing from an empty write buffer!");
}

static void test_cio_write_buffer_queue_head(void)
{
	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	size_t num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(0, num_elements, "Number of elements in write buffer not '0' after initialization of write buffer head!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	unsigned int data1 = 0x12;
	struct cio_write_buffer wb1;
	cio_write_buffer_element_init(&wb1, &data1, sizeof(data1));
	cio_write_buffer_queue_head(&wbh, &wb1);
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(1, num_elements, "Number of elements in write buffer not '1' after inserting first element!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1), cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	const char data2[] = "Hello World!";
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, data2, sizeof(data2));
	cio_write_buffer_queue_head(&wbh, &wb2);
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(2, num_elements, "Number of elements in write buffer not '2' after inserting second element!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1) + sizeof(data2), cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	struct cio_write_buffer *dequeued_wb = cio_write_buffer_queue_dequeue(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(&wb2, dequeued_wb, "First dequeued write buffer is not the write buffer inserted secondly!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(1, num_elements, "Number of elements in write buffer not '1' after dequeueing first element!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1), cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	dequeued_wb = cio_write_buffer_queue_dequeue(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(&wb1, dequeued_wb, "First dequeued write buffer is not the write buffer inserted first!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(0, num_elements, "Number of elements in write buffer not '0' after dequeueing second element!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	dequeued_wb = cio_write_buffer_queue_dequeue(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(NULL, dequeued_wb, "Return value of cio_write_buffer_queue_dequeue not NULL when called on an empty write buffer!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(0, num_elements, "Number of elements in write buffer not '0' after dequeueing from an empty write buffer!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");
}

static void test_cio_write_buffer_peek_and_last(void)
{
	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	size_t num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(0, num_elements, "Number of elements in write buffer not '0' after initialization of write buffer head!");

	unsigned int data1 = 0x12;
	struct cio_write_buffer wb1;
	cio_write_buffer_element_init(&wb1, &data1, sizeof(data1));
	cio_write_buffer_queue_tail(&wbh, &wb1);
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(1, num_elements, "Number of elements in write buffer not '1' after inserting first element!");

	const char data2[] = "Hello World!";
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, data2, sizeof(data2));
	cio_write_buffer_queue_tail(&wbh, &wb2);
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(2, num_elements, "Number of elements in write buffer not '2' after inserting second element!");

	struct cio_write_buffer *peek = cio_write_buffer_queue_peek(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(&wb1, peek, "Peek() gave not the write buffer inserted first!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(2, num_elements, "Number of elements changed after peek()!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1) + sizeof(data2), cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");

	struct cio_write_buffer *last = cio_write_buffer_queue_last(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(&wb2, last, "Last() gave not the write buffer inserted last!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh);
	TEST_ASSERT_EQUAL_MESSAGE(2, num_elements, "Number of elements changed after last()!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1) + sizeof(data2), cio_write_buffer_get_total_size(&wbh), "Write buffer total length not correct!");
}

static void test_cio_write_buffer_peek_from_empty_queue(void)
{
	struct cio_write_buffer wb;
	cio_write_buffer_head_init(&wb);

	struct cio_write_buffer *element = cio_write_buffer_queue_peek(&wb);
	TEST_ASSERT_EQUAL_MESSAGE(NULL, element, "Peeking from empty write buffer did not return NULL!");
}

static void test_cio_write_buffer_last_from_empty_queue(void)
{
	struct cio_write_buffer wb;
	cio_write_buffer_head_init(&wb);

	struct cio_write_buffer *element = cio_write_buffer_queue_last(&wb);
	TEST_ASSERT_EQUAL_MESSAGE(NULL, element, "Calling last() on empty write buffer did not return NULL!");
}

static void test_cio_write_buffer_splice(void)
{
	struct cio_write_buffer wbh_one;
	cio_write_buffer_head_init(&wbh_one);

	unsigned int data1 = 0x12;
	struct cio_write_buffer wb1_one;
	cio_write_buffer_element_init(&wb1_one, &data1, sizeof(data1));
	cio_write_buffer_queue_tail(&wbh_one, &wb1_one);

	const char data2[] = "Hello World!";
	struct cio_write_buffer wb2_one;
	cio_write_buffer_const_element_init(&wb2_one, data2, sizeof(data2));
	cio_write_buffer_queue_tail(&wbh_one, &wb2_one);
	size_t num_elements = cio_write_buffer_get_num_buffer_elements(&wbh_one);
	TEST_ASSERT_EQUAL_MESSAGE(2, num_elements, "Number of elements in write buffer not '2' after inserting second element!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1) + sizeof(data2), cio_write_buffer_get_total_size(&wbh_one), "Write buffer total length not correct!");

	struct cio_write_buffer wbh_two;
	cio_write_buffer_head_init(&wbh_two);

	struct cio_write_buffer wb1_two;
	cio_write_buffer_element_init(&wb1_two, &data1, sizeof(data1));
	cio_write_buffer_queue_tail(&wbh_two, &wb1_two);

	struct cio_write_buffer wb2_two;
	cio_write_buffer_const_element_init(&wb2_two, data2, sizeof(data2));
	cio_write_buffer_queue_tail(&wbh_two, &wb2_two);
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh_two);
	TEST_ASSERT_EQUAL_MESSAGE(2, num_elements, "Number of elements in write buffer not '2' after inserting second element!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1) + sizeof(data2), cio_write_buffer_get_total_size(&wbh_two), "Write buffer total length not correct!");

	cio_write_buffer_splice(&wbh_two, &wbh_one);
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh_two);
	TEST_ASSERT_EQUAL_MESSAGE(0, num_elements, "Number of elements in write buffer two not '0' after splicing to another list!");
	TEST_ASSERT_TRUE_MESSAGE(cio_write_buffer_queue_empty(&wbh_two), "Write buffer not empty after splicing!");
	num_elements = cio_write_buffer_get_num_buffer_elements(&wbh_one);
	TEST_ASSERT_EQUAL_MESSAGE(4, num_elements, "Number of elements in write buffer two not '4' after splicing from another list!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1) + sizeof(data2) + sizeof(data1) + sizeof(data2), cio_write_buffer_get_total_size(&wbh_one), "Write buffer total length not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_total_size(&wbh_two), "Write buffer total length not correct!");

	struct cio_write_buffer *dequeued_wb = cio_write_buffer_queue_dequeue(&wbh_one);
	TEST_ASSERT_EQUAL_MESSAGE(&wb1_one, dequeued_wb, "First dequeued write buffer is not correct after splicing!");
	dequeued_wb = cio_write_buffer_queue_dequeue(&wbh_one);
	TEST_ASSERT_EQUAL_MESSAGE(&wb2_one, dequeued_wb, "Second dequeued write buffer is not correct after splicing!");
	dequeued_wb = cio_write_buffer_queue_dequeue(&wbh_one);
	TEST_ASSERT_EQUAL_MESSAGE(&wb1_two, dequeued_wb, "Third dequeued write buffer is not correct after splicing!");
	dequeued_wb = cio_write_buffer_queue_dequeue(&wbh_one);
	TEST_ASSERT_EQUAL_MESSAGE(&wb2_two, dequeued_wb, "Forth dequeued write buffer is not correct after splicing!");
}

static void test_cio_write_buffer_splice_empty_list(void)
{
	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);

	struct cio_write_buffer wbe;
	cio_write_buffer_element_init(&wbe, NULL, 0);
	cio_write_buffer_queue_tail(&wbh, &wbe);
	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Number of elements in write buffer not correct!");

	struct cio_write_buffer list_to_append;
	cio_write_buffer_head_init(&list_to_append);
	cio_write_buffer_splice(&list_to_append, &wbh);
	TEST_ASSERT_EQUAL_MESSAGE(1, cio_write_buffer_get_num_buffer_elements(&wbh), "Number of elements in write buffer not correct!");
}

static void test_cio_write_buffer_split_and_append(void)
{
	enum { SPLIT_LIST_LENGTH = 6 };
	for (unsigned int split_position = 0; split_position < SPLIT_LIST_LENGTH; split_position++) {
		struct cio_write_buffer wbh_to_split;
		cio_write_buffer_head_init(&wbh_to_split);

		enum { DATA_BUFFER_LENGTH = 100 };
		for (unsigned int i = 0; i < SPLIT_LIST_LENGTH; i++) {
			struct cio_write_buffer *wbe = malloc(sizeof(*wbe));
			TEST_ASSERT_NOT_NULL_MESSAGE(wbe, "allocation of writebuffer element failed!");
			char *data = malloc(DATA_BUFFER_LENGTH);
			TEST_ASSERT_NOT_NULL_MESSAGE(data, "allocation of writebuffer data element failed!");
			int ret = snprintf(data, DATA_BUFFER_LENGTH - 1, "SL_BUFFER%u", i);
			if (ret >= DATA_BUFFER_LENGTH) {
				TEST_FAIL_MESSAGE("Write buffer for split list not filled correctly!");
			}
			cio_write_buffer_element_init(wbe, data, DATA_BUFFER_LENGTH);
			cio_write_buffer_queue_tail(&wbh_to_split, wbe);
		}

		struct cio_write_buffer wbh_to_append;
		cio_write_buffer_head_init(&wbh_to_append);
		enum { APPEND_LIST_LENGTH = 2 };
		for (unsigned int i = 0; i < APPEND_LIST_LENGTH; i++) {
			struct cio_write_buffer *wbe = malloc(sizeof(*wbe));
			TEST_ASSERT_NOT_NULL_MESSAGE(wbe, "allocation of writebuffer element failed!");
			char *data = malloc(DATA_BUFFER_LENGTH);
			TEST_ASSERT_NOT_NULL_MESSAGE(data, "allocation of writebuffer data element failed!");
			int ret = snprintf(data, DATA_BUFFER_LENGTH - 1, "AL_BUFFER%u", i);
			if (ret >= DATA_BUFFER_LENGTH) {
				TEST_FAIL_MESSAGE("Write buffer for append list not filled correctly!");
			}
			cio_write_buffer_element_init(wbe, data, DATA_BUFFER_LENGTH);
			cio_write_buffer_queue_tail(&wbh_to_append, wbe);
		}

		struct cio_write_buffer *e = &wbh_to_split;

		for (unsigned int i = 0; i <= split_position; i++) {
			e = e->next;
		}

		cio_write_buffer_split_and_append(&wbh_to_append, &wbh_to_split, e);
		TEST_ASSERT_EQUAL_MESSAGE(split_position, cio_write_buffer_get_num_buffer_elements(&wbh_to_split), "Number of elements in splitted list not correct!");
		TEST_ASSERT_EQUAL_MESSAGE(split_position * DATA_BUFFER_LENGTH, cio_write_buffer_get_total_size(&wbh_to_split), "Total size of splitted list not correct!");
		TEST_ASSERT_EQUAL_MESSAGE(APPEND_LIST_LENGTH + (SPLIT_LIST_LENGTH - split_position), cio_write_buffer_get_num_buffer_elements(&wbh_to_append), "Number of elements in appended list not correct!");
		TEST_ASSERT_EQUAL_MESSAGE((APPEND_LIST_LENGTH + (SPLIT_LIST_LENGTH - split_position)) * DATA_BUFFER_LENGTH, cio_write_buffer_get_total_size(&wbh_to_append), "Total size of appended list not correct!");

		e = wbh_to_append.next;
		for (unsigned i = 0; i < APPEND_LIST_LENGTH; i++) {
			char buf[DATA_BUFFER_LENGTH];
			int ret = snprintf(buf, sizeof(buf), "AL_BUFFER%u", i);
			if (ret >= DATA_BUFFER_LENGTH) {
				TEST_FAIL_MESSAGE("Check buffer for append list not filled correctly!");
			}
			TEST_ASSERT_EQUAL_STRING_MESSAGE(buf, e->data.element.data, "Data in merged append buffer not correct!");
			e = e->next;
		}
		for (unsigned i = split_position; i < SPLIT_LIST_LENGTH; i++) {
			char buf[DATA_BUFFER_LENGTH];
			int ret = snprintf(buf, sizeof(buf), "SL_BUFFER%u", i);
			if (ret >= DATA_BUFFER_LENGTH) {
				TEST_FAIL_MESSAGE("Check buffer for split list not filled correctly!");
			}
			TEST_ASSERT_EQUAL_STRING_MESSAGE(buf, e->data.element.data, "Data in merged split buffer not correct!");
			e = e->next;
		}

		while (!cio_write_buffer_queue_empty(&wbh_to_split)) {
			struct cio_write_buffer *remaining_buffer_in_split_list = cio_write_buffer_queue_dequeue(&wbh_to_split);
			free(remaining_buffer_in_split_list->data.element.data);
			free(remaining_buffer_in_split_list);
		}

		while (!cio_write_buffer_queue_empty(&wbh_to_append)) {
			struct cio_write_buffer *remaining_buffer_in_append_list = cio_write_buffer_queue_dequeue(&wbh_to_append);
			free(remaining_buffer_in_append_list->data.element.data);
			free(remaining_buffer_in_append_list);
		}
	}
}

static void test_cio_write_buffer_split_and_append_empty_list(void)
{
	struct cio_write_buffer wbh_to_split;
	cio_write_buffer_head_init(&wbh_to_split);

	struct cio_write_buffer wbh_to_append;
	cio_write_buffer_head_init(&wbh_to_append);

	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, "HELLO", sizeof("HELLO"));
	cio_write_buffer_queue_tail(&wbh_to_append, &wb1);
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, "World", sizeof("World"));
	cio_write_buffer_queue_tail(&wbh_to_append, &wb2);
	size_t size_before_append = cio_write_buffer_get_total_size(&wbh_to_append);

	struct cio_write_buffer *e = &wbh_to_split;

	cio_write_buffer_split_and_append(&wbh_to_append, &wbh_to_split, e);
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_num_buffer_elements(&wbh_to_split), "Number of elements in splitted list not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_total_size(&wbh_to_split), "Total size of splitted list not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(2, cio_write_buffer_get_num_buffer_elements(&wbh_to_append), "Number of elements in appended list not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(size_before_append, cio_write_buffer_get_total_size(&wbh_to_append), "Total size of appended list not correct!");
}

static void test_cio_write_buffer_split_and_append_head(void)
{
	struct cio_write_buffer wbh_to_split;
	cio_write_buffer_head_init(&wbh_to_split);
	struct cio_write_buffer wb_split;
	cio_write_buffer_const_element_init(&wb_split, "Hello", sizeof("Hello"));
	cio_write_buffer_queue_tail(&wbh_to_split, &wb_split);

	struct cio_write_buffer wbh_to_append;
	cio_write_buffer_head_init(&wbh_to_append);
	struct cio_write_buffer wb1;
	cio_write_buffer_const_element_init(&wb1, "HELLO", sizeof("HELLO"));
	cio_write_buffer_queue_tail(&wbh_to_append, &wb1);
	struct cio_write_buffer wb2;
	cio_write_buffer_const_element_init(&wb2, "World", sizeof("World"));
	cio_write_buffer_queue_tail(&wbh_to_append, &wb2);

	size_t split_size_before = cio_write_buffer_get_total_size(&wbh_to_split);
	size_t append_size_before = cio_write_buffer_get_total_size(&wbh_to_append);

	struct cio_write_buffer *e = &wbh_to_split;

	cio_write_buffer_split_and_append(&wbh_to_append, &wbh_to_split, e);
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_num_buffer_elements(&wbh_to_split), "Number of elements in splitted list not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, cio_write_buffer_get_total_size(&wbh_to_split), "Total size of splitted list not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(2 + 1, cio_write_buffer_get_num_buffer_elements(&wbh_to_append), "Number of elements in appended list not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(append_size_before + split_size_before, cio_write_buffer_get_total_size(&wbh_to_append), "Total size of appended list not correct!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_cio_write_buffer_element_init);
	RUN_TEST(test_cio_write_buffer_const_element_init);
	RUN_TEST(test_cio_write_buffer_head_init);
	RUN_TEST(test_cio_write_buffer_queue_tail);
	RUN_TEST(test_cio_write_buffer_queue_head);
	RUN_TEST(test_cio_write_buffer_peek_and_last);
	RUN_TEST(test_cio_write_buffer_peek_from_empty_queue);
	RUN_TEST(test_cio_write_buffer_last_from_empty_queue);
	RUN_TEST(test_cio_write_buffer_splice);
	RUN_TEST(test_cio_write_buffer_splice_empty_list);
	RUN_TEST(test_cio_write_buffer_split_and_append);
	RUN_TEST(test_cio_write_buffer_split_and_append_empty_list);
	RUN_TEST(test_cio_write_buffer_split_and_append_head);
	return UNITY_END();
}
