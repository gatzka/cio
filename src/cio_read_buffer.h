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

/**
 * @file
 * @brief Functions to access a read buffer for library users.
 */

/**
 * @brief An opaque structure encapsulationg a read buffer.
 */
struct cio_read_buffer {
	/**
	 * @privatesection
	 */
	uint8_t *data;
	uint8_t *end;
	uint8_t *add_ptr;
	uint8_t *fetch_ptr;
	size_t bytes_transferred;
};

/**
 * @brief Provides the number of bytes that are not read yet.
 * @param rb The read buffer that is asked.
 * @return The number of bytes in read buffer that are not yet read.
 */
static inline size_t cio_read_buffer_unread_bytes(const struct cio_read_buffer *rb)
{
	return rb->add_ptr - rb->fetch_ptr;
}

/**
 * @brief Provides the space currently not filled with data.
 * @param rb The read buffer that is asked.
 * @return The length in bytes of available space.
 */
static inline size_t cio_read_buffer_space_available(const struct cio_read_buffer *rb)
{
	return rb->end - rb->add_ptr;
}

/**
 * @brief Initializes a read buffer.
 * @param rb The read buffer to be initialized.
 * @param data The memory the read buffer operates on.
 * @param size The size of the memory the read buffer operates on.
 * @return ::cio_success for success.
 */
static inline enum cio_error cio_read_buffer_init(struct cio_read_buffer *rb, void *data, size_t size)
{
	if (unlikely((rb == NULL) || (data == NULL))) {
		return cio_invalid_argument;
	}

	rb->data = data;
	rb->end = (uint8_t *)data + size;
	rb->fetch_ptr = data;
	rb->add_ptr = data;

	return cio_success;
}

/**
 * @brief Provides the pointer from where to read data.
 *
 * The amount of data which can be read is provided by ::cio_read_buffer_get_transferred_bytes.
 * @param rb The read buffer to be asked.
 * @return The pointer from where to read.
 */
static inline uint8_t *cio_read_buffer_get_read_ptr(const struct cio_read_buffer *rb)
{
	return rb->fetch_ptr - rb->bytes_transferred;
}

/**
 * @brief Provides the number of bytes transferred during the last read operation.
 * @param rb The read buffer to be asked.
 * @return The number of bytes transferred during the last read operation.
 */
static inline size_t cio_read_buffer_get_transferred_bytes(const struct cio_read_buffer *rb)
{
	return rb->bytes_transferred;
}

/**
 * @brief Provides the size of the read buffer.
 * @param rb The read buffer to be asked.
 * @return The size of the read buffer in bytes.
 */
static inline size_t cio_read_buffer_size(const struct cio_read_buffer *rb)
{
	return rb->end - rb->data;
}

#ifdef __cplusplus
}
#endif

#endif
