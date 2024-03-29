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

#ifndef cio_write_buffer_H
#define cio_write_buffer_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cio/compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Functions to manage a write buffer.
 *
 * A write buffer comprises of a chain of buffer elements. Starting with a write buffer head,
 * multiple write buffer elements can be chained together. There are functions available like
 * queueing a write buffer element @ref cio_write_buffer_queue_tail "to the end" of a write buffer.
 *
 * If available, platform specific write functions shall utilize scatter/gather I/O
 * functions like writev() to send a write buffer chain in a single chunk.
 */

/**
 * @brief Structure to build up a write buffer chain.
 *
 * This structure is used for both a write buffer head and a write buffer element holding data.
 */
struct cio_write_buffer {
	/**
	 * @privatesection
	 */
	struct cio_write_buffer *next;
	struct cio_write_buffer *prev;
	union {
		struct {
			union {
				const void *const_data;
				void *data;
			};
			size_t length;
		} element;

		struct {
			size_t q_len;
			size_t total_length;
		} head;
	} data;
};

/**
 * @brief Insert a new write buffer element into the write buffer chain.
 * @param wbh The write buffer chain that is manipulated.
 * @param new_wb The write buffer element that shall be inserted
 * @param prev_wb The write buffer element after which the new write buffer element shall be inserted.
 * @param next_wb The write buffer element before which the new write buffer element shall be inserted.
 */
static inline void cio_write_buffer_insert(struct cio_write_buffer *wbh,
                                           struct cio_write_buffer *new_wb,
                                           struct cio_write_buffer *prev_wb,
                                           struct cio_write_buffer *next_wb)
{
	new_wb->next = next_wb;
	new_wb->prev = prev_wb;
	next_wb->prev = prev_wb->next = new_wb;
	wbh->data.head.q_len++;
	wbh->data.head.total_length += new_wb->data.element.length;
}
/**
 * @brief Queue a new write buffer element before a write buffer element in a write buffer chain.
 * @param wbh The write buffer chain that is manipulated.
 * @param next_wb The write buffer element before which the new write buffer element shall be inserted.
 * @param new_wb The write buffer element that shall be inserted
 */
static inline void cio_write_buffer_queue_before(struct cio_write_buffer *wbh,
                                                 struct cio_write_buffer *next_wb,
                                                 struct cio_write_buffer *new_wb)
{
	cio_write_buffer_insert(wbh, new_wb, next_wb->prev, next_wb);
}

/**
 * @brief Queue a new write buffer element after a write buffer element in a write buffer chain.
 * @param wbh The write buffer chain that is manipulated.
 * @param prev_wb The write buffer element after which the new write buffer element shall be inserted.
 * @param new_wb The write buffer element that shall be inserted
 */
static inline void cio_write_buffer_queue_after(struct cio_write_buffer *wbh,
                                                struct cio_write_buffer *prev_wb,
                                                struct cio_write_buffer *new_wb)
{
	cio_write_buffer_insert(wbh, new_wb, prev_wb, prev_wb->next);
}

/**
 * @brief Queue a new write buffer element at the head of a write buffer chain.
 * @param wbh The write buffer chain that is manipulated.
 * @param new_wb The write buffer element that shall be inserted
 */
static inline void cio_write_buffer_queue_head(struct cio_write_buffer *wbh,
                                               struct cio_write_buffer *new_wb)
{
	cio_write_buffer_queue_after(wbh, wbh, new_wb);
}

/**
 * @brief Queue a new write buffer element at the tail of a write buffer chain.
 * @param wbh The write buffer chain that is manipulated.
 * @param new_wb The write buffer element that shall be inserted
 */
static inline void cio_write_buffer_queue_tail(struct cio_write_buffer *wbh,
                                               struct cio_write_buffer *new_wb)
{
	cio_write_buffer_queue_before(wbh, wbh, new_wb);
}

/**
 * @brief Determine if the write buffer chain is empty.
 * @param wbh The write buffer chain that is asked.
 * @return @c true if the chain is empty, @c false otherwise.
 */
static inline bool cio_write_buffer_queue_empty(const struct cio_write_buffer *wbh)
{
	return wbh->next == wbh;
}

/**
 * @brief Determine if the write buffer chain is empty.
 * @param wbh The write buffer chain that is asked.
 * @param wbe The write buffer element that is asked if it is the last in the queue.
 * @return @c true if the chain is empty, @c false otherwise.
 */
static inline bool cio_write_buffer_is_last_element(const struct cio_write_buffer *wbh, const struct cio_write_buffer *wbe)
{
	return wbe->prev == wbh;
}

/**
 * @brief Access the first element of a write buffer chain without removing it.
 * @param wbh The write buffer chain that is asked.
 * @return The first write buffer element if available, @c NULL otherwise.
 */
static inline struct cio_write_buffer *cio_write_buffer_queue_peek(const struct cio_write_buffer *wbh)
{
	struct cio_write_buffer *wbe = wbh->next;

	if (wbe == wbh) {
		return NULL;
	}

	return wbe;
}

/**
 * @brief Access the last element of a write buffer chain without removing it.
 * @param wbh The write buffer chain that is asked.
 * @return The last write buffer element if available, @c otherwise.
 */
static inline struct cio_write_buffer *cio_write_buffer_queue_last(const struct cio_write_buffer *wbh)
{
	struct cio_write_buffer *wbe = wbh->prev;

	if (wbe == wbh) {
		wbe = NULL;
	}

	return wbe;
}

/**
 * @brief Removes a write buffer element from the write buffer chain
 * @param wbh The write buffer chain that is manipulated.
 * @param wbe The element that shall be removed.
 */
static inline void cio_write_buffer_unlink(struct cio_write_buffer *wbh, const struct cio_write_buffer *wbe)
{
	struct cio_write_buffer *next = NULL;
	struct cio_write_buffer *prev = NULL;

	wbh->data.head.q_len--;
	wbh->data.head.total_length -= wbe->data.element.length;
	next = wbe->next;
	prev = wbe->prev;
	next->prev = prev;
	prev->next = next;
}

/**
 * @brief Accesses and removes the first element from the write buffer chain
 * @param wbh The write buffer chain that is manipulated.
 * @return The first element of the queue, @c NULL if empty.
 */
static inline struct cio_write_buffer *cio_write_buffer_queue_dequeue(struct cio_write_buffer *wbh)
{
	struct cio_write_buffer *wbe = cio_write_buffer_queue_peek(wbh);
	if (wbe) {
		cio_write_buffer_unlink(wbh, wbe);
	}

	return wbe;
}

/**
 * @brief Initializes a write buffer head.
 * @param wbh The write buffer head to be initialized.
 */
static inline void cio_write_buffer_head_init(struct cio_write_buffer *wbh)
{
	wbh->prev = wbh;
	wbh->next = wbh;
	wbh->data.head.q_len = 0;
	wbh->data.head.total_length = 0;
}

/**
 * @brief Initializes a write buffer element with const data.
 * @param wbe The write buffer element to be initialized.
 * @param data A pointer to the data the write buffer element shall be handled.
 * @param length The length in bytes of @p data.
 */
static inline void cio_write_buffer_const_element_init(struct cio_write_buffer *wbe, const void *data, size_t length)
{
	wbe->data.element.const_data = data;
	wbe->data.element.length = length;
}

/**
 * @brief Initializes a write buffer element with non-const data.
 * @param wbe The write buffer element to be initialized.
 * @param data A pointer to the data the write buffer element shall be handled.
 * @param length The length in bytes of @p data.
 */
static inline void cio_write_buffer_element_init(struct cio_write_buffer *wbe, void *data, size_t length)
{
	wbe->data.element.data = data;
	wbe->data.element.length = length;
}

/**
 * @brief Appends the write buffer elements of @p list to @p head.
 * @param list The elements which shall be appended to @p head.
 * @param head The list that shall pick up the elements of @p list.
 */
static inline void cio_write_buffer_splice(struct cio_write_buffer *list, struct cio_write_buffer *head)
{
	if (!cio_write_buffer_queue_empty(list)) {
		struct cio_write_buffer *new_last = list->prev;
		struct cio_write_buffer *new_first = list->next;
		struct cio_write_buffer *last = head->prev;

		last->next = list->next;
		new_first->prev = last;
		new_last->next = head;
		head->prev = new_last;

		head->data.head.q_len += list->data.head.q_len;
		head->data.head.total_length += list->data.head.total_length;

		cio_write_buffer_head_init(list);
	}
}

/**
 * @brief Get the total size of data in the write buffer.
 * @param head The write buffer from which the length should be gathered.
 * @return Returns the total size of data in this buffer.
 */
static inline size_t cio_write_buffer_get_total_size(const struct cio_write_buffer *head)
{
	return head->data.head.total_length;
}

/**
 * @brief Get the number of individual buffer elements queued in this write buffer.
 * @param head The write buffer from which the number of elements should be gathered.
 * @return Returns the number of individual write buffer elements.
 */
static inline size_t cio_write_buffer_get_num_buffer_elements(const struct cio_write_buffer *head)
{
	return head->data.head.q_len;
}

static inline void cio_write_buffer_split_and_append(struct cio_write_buffer *to_head, struct cio_write_buffer *from_head, struct cio_write_buffer *wbe)
{
	if (cio_unlikely(cio_write_buffer_queue_empty(from_head))) {
		return;
	}

	if (cio_unlikely(from_head == wbe)) {
		wbe = from_head->next;
	}

	struct cio_write_buffer *to_last = to_head->prev;
	struct cio_write_buffer *from_last = from_head->prev;
	size_t tail_length = 0;
	size_t tail_qlen = 0;

	const struct cio_write_buffer *element = wbe;
	while (element != from_head) {
		tail_length += element->data.element.length;
		tail_qlen++;
		element = element->next;
	}

	struct cio_write_buffer *wbe_prev = wbe->prev;
	wbe_prev->next = from_head;
	from_head->prev = wbe_prev;
	from_head->data.head.q_len -= tail_qlen;
	from_head->data.head.total_length -= tail_length;

	from_last->next = to_head;
	to_head->prev = from_last;
	to_last->next = wbe;
	wbe->prev = to_last;
	to_head->data.head.q_len += tail_qlen;
	to_head->data.head.total_length += tail_length;
}

#ifdef __cplusplus
}
#endif

#endif
