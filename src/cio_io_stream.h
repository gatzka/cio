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
#include <stdint.h>

#include "cio_error_code.h"
#include "cio_read_buffer.h"
#include "cio_write_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the definitions all users of a cio_io_stream
 * need to know.
 */

struct cio_io_stream;

/**
 * @brief The type of a function passed to all cio_io_stream read callback functions.
 * 
 * @param io_stream The cio_io_stream the read operation was called on.
 * @param handler_context The context the functions works on.
 * @param err If err != ::CIO_SUCCESS, the read operation failed.
 * @param buffer The buffer that was filled.
 */
typedef void (*cio_io_stream_read_handler)(struct cio_io_stream *io_stream, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer);

/**
 * @brief The type of a function passed to all cio_io_stream write callback functions.
 * 
 * @param io_stream The cio_io_stream the write operation was called on.
 * @param handler_context The context the functions works on.
 * @param buffer The buffer which should have been written.
 * @param err If err != ::CIO_SUCCESS, the write operation failed.
 * @param bytes_transferred The number of bytes transferred.
 */
typedef void (*cio_io_stream_write_handler)(struct cio_io_stream *io_stream, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err, size_t bytes_transferred);

/**
 * @brief This structure describes the interface all implementations
 * have to fulfill.
 */
struct cio_io_stream {

	/**
	 * @brief Read upto @p count bytes into the buffer @p buf starting
	 * with offset @p offset.
	 *
	 * @param io_stream A pointer to the cio_io_stream of the on which the operation should be performed.
	 * @param buffer The buffer to be filled.
	 * @param handler The callback function to be called when the read
	 *                request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 *                        useful inside @p handler.
	 * @return ::CIO_SUCCESS for success.
	 */
	enum cio_error (*read_some)(struct cio_io_stream *io_stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context);

	/**
	 * @brief Writes upto @p count buffers to the stream.
	 *
	 * @p handler might be called if only parts of @p buffer had been written.
	 *
	 * @param io_stream A pointer to the cio_io_stream of the on which the operation should be performed.
	 * @param buf The buffer where the data is written from. Please note that the memory @p buf points to
	 *            must be retained until @p handler was called.
	 * @param count The number of to write.
	 * @param handler The callback function to be called when the write
	 *                request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 *                        useful inside @p handler.
	 * @return ::CIO_SUCCESS for success.
	 */
	enum cio_error (*write_some)(struct cio_io_stream *io_stream, const struct cio_write_buffer *buf, cio_io_stream_write_handler handler, void *handler_context);

	/**
	 * @brief Closes the stream.
	 *
	 * Implementations implementing this interface are strongly
	 * encouraged to flush any write buffers and to free other resources
	 * associated with this stream.
	 *
	 * @param io_stream A pointer to the cio_io_stream of the on which the operation should be performed.
	 *
	 * @return ::CIO_SUCCESS for success.
	 */
	enum cio_error (*close)(struct cio_io_stream *io_stream);

	/**
	 * @privatesection
	 */
	cio_io_stream_read_handler read_handler;
	void *read_handler_context;
	struct cio_read_buffer *read_buffer;
	const struct cio_write_buffer *write_buffer;
	cio_io_stream_write_handler write_handler;
	void *write_handler_context;
};

#ifdef __cplusplus
}
#endif

#endif
