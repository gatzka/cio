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

#ifndef CIO_IO_STREAM_H
#define CIO_IO_STREAM_H

#include <stddef.h>

#include "cio_io_vector.h"
#include "cio_stream_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the definitions all users of a cio_io_stream
 * need to know.
 */

/**
 * @brief This structure describes the interface all implementations
 * have to fulfill.
 */
struct cio_io_stream {
	/**
	 * @brief The context pointer which is passed to the
	 * cio_io_stream::read and cio_io_stream::close functions.
	 */
	void *context;

	/**
	 * @brief Read upto @p count bytes into the buffer @p buf starting
	 * with offset @p offset.
	 *
	 * @param context A pointer to the cio_io_stream::context of the
	 * implementation implementing this interface.
	 * @param buf The buffer to be filled.
	 * @param count The maximum number of bytes to read.
	 * @param handler The callback function to be called when the read
	 * request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*read_some)(void *context, void *buf, size_t count, cio_stream_handler handler, void *handler_context);

	/**
	 * @brief Writes @p count buffers to the stream.
	 *
	 * @param context A pointer to the cio_io_stream::context of the
	 * implementation implementing this interface.
	 * @param io_vec An array of cio_io_vector buffers.
	 * @param count Number of cio_io_vector buffers in @p io_vec.
	 * @param handler The callback function to be called when the write
	 * request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*writev_some)(void *context, struct cio_io_vector *io_vec, unsigned int count, cio_stream_handler handler, void *handler_context);

	/**
	 * @brief Closes the stream.
	 *
	 * Implementations implementing this interface are strongly
	 * encouraged to flush any write buffers and to free other resources
	 * associated with this stream.
	 */
	void (*close)(void *context);
};

#ifdef __cplusplus
}
#endif

#endif
