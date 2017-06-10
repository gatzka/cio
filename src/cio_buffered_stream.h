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

#ifndef CIO_BUFFERED_STREAM_H
#define CIO_BUFFERED_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "cio_buffer_allocator.h"
#include "cio_error_code.h"
#include "cio_io_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the declarations all users of a
 * cio_buffered_stream need to know.
 *
 * A cio_buffered_stream is always greedy to fill its internal read
 * buffer. Therefore, it greatly reduces the amount of read calls into
 * the underlying operating system.
 *
 * Additonally, a cio_buffered_stream gives you the concept of reading
 * an exact number of bytes or reading until a delimiter string is
 * encountered. Both concepts are useful when parsing protocols.
 *
 * A @p writev call into a buffered_stream only writes into the internal
 * write buffer. The write buffer is flushed only if the internal write
 * buffer is called or @p flush is called explicitly.
 */

struct cio_buffered_stream;

/**
 * @brief The type of a function passed to all cio_buffered_stream read callback functions.
 * 
 * @param context The cio_buffered_stream the read operation was called on.
 * @param handler_context The context the functions works on.
 * @param err If err != ::cio_success, the read operation failed.
 * @param buf A pointer to the begin of the buffer where the data was read in. 
 * @param bytes_transferred The number of bytes transferred into @p buf.
 */
typedef void (*cio_buffered_stream_read_handler)(struct cio_buffered_stream *context, void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred);

/**
 * @brief The type of a function passed to all cio_buffered_stream write callback functions.
 * 
 * @param context The cio_buffered_stream the write operation was called on.
 * @param handler_context The context the functions works on.
 * @param err If err != ::cio_success, the write operation failed.
 * @param bytes_transferred The number of bytes transferred.
 */
typedef void (*cio_buffered_stream_write_handler)(struct cio_buffered_stream *context, void *handler_context, enum cio_error err, size_t bytes_transferred);

/**
 * Interface description for implementing buffered I/O.
 */
struct cio_buffered_stream {

	/**
	 * @brief Initializes a cio_buffered_stream.
	 *
	 * @param context A pointer to the cio_buffered_stream::context of the
	 * implementation implementing this interface.
	 * @param io A pointer to an cio_io_stream which is used to fill the
	 * internal read buffer an to which the internal write buffer will
	 * be flushed.
	 * @param allocator The cio_buffer_allocator which is used to allocate the
	 * internal read and write buffers.
	 * @return Zero for success, -1 in case of an error.
	 */
	int (*init)(struct cio_buffered_stream *context, const struct cio_io_stream *io, const struct cio_buffer_allocator *allocator);

	/**
	 * @brief Call @p handler if exactly @p num bytes are read.
	 *
	 * @param context A pointer to the cio_buffered_stream::context of the
	 * implementation implementing this interface.
	 * @param num The number of bytes to be read when @p handler will be called.
	 * @param handler The callback function to be called when the read
	 * request is fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*read_exactly)(struct cio_buffered_stream *context, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);

	/**
	 * @brief Read upto @p count bytes into the buffer @p buf starting
	 * with offset @p offset.
	 *
	 * @param context A pointer to the cio_buffered_stream the the read operates on.
	 * @param buf The buffer to be filled.
	 * @param offset The start offset in @p buf at which the data is
	 * written.
	 * @param count The maximum number of bytes to read.
	 * @param handler The callback function to be called when the read
	 * request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*read)(struct cio_buffered_stream *context, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);

	/**
	 * @brief Call @p handler if delimiter @p delim is encountered.
	 *
	 * @param context A pointer to the cio_buffered_stream the read operates on.
	 * @param delim A zero terminated string containing the delimiter to be found.
	 * @param handler The callback function to be called when the read
	 * request is fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*read_until)(struct cio_buffered_stream *context, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context);

	/**
	 * @brief Writes @p count bytes to the buffered stream.
	 *
	 * Please note that the data written is not immediatly forwarded to
	 * the underlying cio_io_stream. Call cio_buffered_stream::flush to do so.
	 *
	 * @param context A pointer to the cio_buffered_stream the write operates on.
	 * @param buf The buffer where the data is written from.
	 * @param count The number of to write.
	 * @param handler The callback function to be called when the write
	 * request is fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*write)(struct cio_buffered_stream *context, const void *buf, size_t count, cio_buffered_stream_write_handler handler, void *handler_context);

	/**
	 * Drains the data in the write buffer out to the underlying
	 * cio_io_stream.
	 */
	void (*flush)(struct cio_buffered_stream *context);

	/**
	 * @brief Closes the stream.
	 *
	 * Implementations implementing this interface are strongly
	 * encouraged to flush any write buffers and to free other resources
	 * associated with this stream.
	 */
	void (*close)(struct cio_buffered_stream *context);
};

#ifdef __cplusplus
}
#endif

#endif
