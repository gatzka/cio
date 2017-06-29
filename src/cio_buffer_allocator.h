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

#ifndef CIO_BUFFER_ALLOCATOR_H
#define CIO_BUFFER_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the declarations all users of a buffer_allocator
 * need to know.
 */

struct cio_buffer {
	/**
	 * The address of the buffer.
	 */
	void *address;

	/**
	 * The size of the buffer in bytes.
	 */
	size_t size;
};

/**
 * Interface description for a buffer allocator.
 */
struct cio_buffer_allocator {

	/**
	 * @brief Allocates a buffer.
	 *
	 * @param context A pointer to the cio_buffer_allocator::context of the
	 * implementation implementing this interface.
	 * @param size The requested size of the buffer. The allocated
	 * buffer might be greater than requested.
	 * @return A cio_buffer containing the address of the buffer and the
	 * real size of the buffer. If the request can't be fulfilled,
	 * cio_buffer::address is @p NULL.
	 */
	struct cio_buffer (*alloc)(struct cio_buffer_allocator *context, size_t size);

	/**
	 * @brief Frees a buffer.
	 *
	 * @param context A pointer to the cio_buffer_allocator::context of the
	 * implementation implementing this interface.
	 * @param ptr Address of the buffer to be freed.
	 */
	void (*free)(struct cio_buffer_allocator *context, void *ptr);
};

struct cio_buffer_allocator *get_system_allocator(void);

#ifdef __cplusplus
}
#endif

#endif
