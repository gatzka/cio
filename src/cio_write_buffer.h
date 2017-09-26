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

#ifndef CIO_WRITE_BUFFER_H
#define CIO_WRITE_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cio_write_buffer {
	struct cio_write_buffer *next;
	struct cio_write_buffer *prev;
	const void *data;
	size_t length;
};

struct cio_write_buffer_head {
	struct cio_write_buffer *next;
	struct cio_write_buffer *prev;
	size_t q_len;
};

static inline void cio_write_buffer_insert(struct cio_write_buffer *new_wb,
                                           struct cio_write_buffer *prev_wb, struct cio_write_buffer *next_wb,
                                           struct cio_write_buffer_head *wbh)
{
	new_wb->next = next_wb;
	new_wb->prev = prev_wb;
	next_wb->prev = prev_wb->next = new_wb;
	wbh->q_len++;
}

static inline void cio_write_buffer_queue_before(struct cio_write_buffer_head *wbh,
                                                 struct cio_write_buffer *next_wb,
                                                 struct cio_write_buffer *new_wb)
{
	cio_write_buffer_insert(new_wb, next_wb->prev, next_wb, wbh);
}

static inline void cio_write_buffer_queue_after(struct cio_write_buffer_head *wbh,
                                                struct cio_write_buffer *prev_wb,
                                                struct cio_write_buffer *new_wb)
{
	cio_write_buffer_insert(new_wb, prev_wb, prev_wb->next, wbh);
}

static inline void cio_write_buffer_queue_head(struct cio_write_buffer_head *wbh,
                                               struct cio_write_buffer *new_wb)
{
	cio_write_buffer_queue_after(wbh, (struct cio_write_buffer *)wbh, new_wb);
}

static inline void cio_write_buffer_queue_tail(struct cio_write_buffer_head *wbh,
                                               struct cio_write_buffer *new_wb)
{
	cio_write_buffer_queue_before(wbh, (struct cio_write_buffer *)wbh, new_wb);
}

static inline bool cio_write_buffer_queue_empty(const struct cio_write_buffer_head *wbh)
{
	return wbh->next == (const struct cio_write_buffer *)wbh;
}

static inline struct cio_write_buffer *cio_write_buffer_queue_peek(const struct cio_write_buffer_head *wbh)
{
	struct cio_write_buffer *wb = wbh->next;

	if (wb == (const struct cio_write_buffer *)wbh) {
		wb = NULL;
	}

	return wb;
}

static inline bool cio_write_buffer_queue_is_last(const struct cio_write_buffer_head *wbh, const struct cio_write_buffer *wb)
{
	return wb->next == (const struct cio_write_buffer *)wbh;
}

static inline void cio_write_buffer_unlink(struct cio_write_buffer *wb, struct cio_write_buffer_head *wbh)
{
	struct cio_write_buffer *next;
	struct cio_write_buffer *prev;

	wbh->q_len--;
	next = wb->next;
	prev = wb->prev;
	next->prev = prev;
	prev->next = next;
}

static inline struct cio_write_buffer *cio_write_buffer_queue_dequeue(struct cio_write_buffer_head *wbh)
{
	struct cio_write_buffer *wb = cio_write_buffer_queue_peek(wbh);
	if (wb) {
		cio_write_buffer_unlink(wb, wbh);
	}

	return wb;
}

static inline void cio_write_buffer_head_init(struct cio_write_buffer_head *wbh)
{
	wbh->prev = (struct cio_write_buffer *)wbh;
	wbh->next = (struct cio_write_buffer *)wbh;
	wbh->q_len = 0;
}

static inline void cio_write_buffer_init(struct cio_write_buffer *wb, const void *data, size_t length)
{
	wb->data = data;
	wb->length = length;
	wb->next = NULL;
	wb->prev = NULL;
}

#ifdef __cplusplus
}
#endif

#endif
