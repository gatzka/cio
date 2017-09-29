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

#ifndef CIO_READ_BUFFER_H
#define CIO_READ_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "cio_compiler.h"
#include "cio_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_read_buffer {
	/**
	 * @privatesection
	 */
	uint8_t *data;
	size_t size;
	size_t unread_bytes;
	uint8_t *read_from_ptr;
};

static inline size_t cio_read_buffer_unread_bytes(const struct cio_read_buffer *rb)
{
	return rb->unread_bytes;
}

static inline size_t cio_read_buffer_space_available(const struct cio_read_buffer *rb)
{
	return rb->size - cio_read_buffer_unread_bytes(rb);
}

static inline enum cio_error cio_read_buffer_init(struct cio_read_buffer *rb, void *data, size_t size)
{
	if (unlikely((rb == NULL) || (data == NULL))) {
		return cio_invalid_argument;
	}

	rb->data = data;
	rb->size = size;
	rb->unread_bytes = 0;
	rb->read_from_ptr = data;

	return cio_success;
}

#ifdef __cplusplus
}
#endif

#endif
