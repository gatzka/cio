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

#ifndef CIO_BUFFERED_STREAM_H
#define CIO_BUFFERED_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cio/error_code.h"
#include "cio/export.h"
#include "cio/io_stream.h"
#include "cio/read_buffer.h"
#include "cio/write_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Provides efficient reading and writing from or to an ::cio_io_stream.
 *
 * A cio_buffered_stream is always greedy to fill its internal read
 * buffer. Therefore, it greatly reduces the amount of read calls into
 * the underlying operating system.
 *
 * Additonally, a cio_buffered_stream gives you the concept of reading
 * at least a number of bytes or reading until a delimiter string is
 * encountered. Both concepts are useful when parsing protocols.
 *
 * The callback of a @p write call into a buffered_stream is only called
 * if either the provided buffer was completely written or an error occured.
 * After reading out data from the read_buffer, you must mark the data as consumed
 * by calling @ref cio_read_buffer_consume.
 */
struct cio_buffered_stream;

/**
 * @brief The type of a function passed to all cio_buffered_stream read callback functions.
 * 
 * @param buffered_stream The cio_buffered_stream the read operation was called on.
 * @param handler_context The context the functions works on.
 * @param err If err != ::CIO_SUCCESS, the read operation failed, if err == ::CIO_EOF, the peer closed the stream.
 * @param buffer The buffer where the data read is stored.
 * @param num_bytes The number of bytes until the delimiter was found (when called via @ref cio_buffered_stream_read_until "read_until()"), or
 * the number of bytes that should have been read at least (when called via @ref cio_buffered_stream_read_at_least "read_at_least()")
 */
typedef void (*cio_buffered_stream_read_handler_t)(struct cio_buffered_stream *buffered_stream, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer, size_t num_bytes);

/**
 * @brief The type of a function passed to all cio_buffered_stream write callback functions.
 * 
 * @param buffered_stream The cio_buffered_stream the write operation was called on.
 * @param handler_context The context the functions works on.
 * @param err If err != ::CIO_SUCCESS, the write operation failed.
 */
typedef void (*cio_buffered_stream_write_handler_t)(struct cio_buffered_stream *buffered_stream, void *handler_context, enum cio_error err);

/**
 * @private
 */
enum cio_bs_state {
	CIO_BS_OPEN = 0,
	CIO_BS_CLOSED,
	CIO_BS_AGAIN
};

/**
 * @private
 */
union cio_read_info {
	size_t bytes_to_read;
	struct {
		const char *delim;
		size_t delim_length;
	} until;
};

/**
 * Interface description for implementing buffered a buffered stream.
 */
struct cio_buffered_stream {

	/**
	 * @privatesection
	 */
	struct cio_io_stream *stream;

	struct cio_read_buffer *read_buffer;
	cio_buffered_stream_read_handler_t read_handler;
	void *read_handler_context;

	enum cio_bs_state (*read_job)(struct cio_buffered_stream *buffered_stream);

	union cio_read_info read_info;

	cio_buffered_stream_write_handler_t write_handler;
	void *write_handler_context;

	struct cio_write_buffer *original_wbh;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	enum cio_error last_error;
	unsigned int callback_is_running;
	bool shall_close;
};

/**
 * @brief Initializes a cio_buffered_stream.
 *
 * @param bs The cio_buffered_stream that should be initialized.
 * @param stream The IO stream that should be used to read from
 * and to write to.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                                   struct cio_io_stream *stream);

/**
 * @brief Call @p handler if at least @p num bytes are read.
 *
 * Schedule a read job which calls @p handler if at least @p num bytes are available to consume.
 *
 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
 * @param buffer The buffer that should be used for reading.
 * @param num The number of bytes to be read at least when @p handler will be called.
 * @param handler The callback function to be called when the read
 * request is fulfilled.
 * @param handler_context A pointer to a context which might be
 * useful inside @p handler.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_buffered_stream_read_at_least(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context);

/**
 * @anchor cio_buffered_stream_read_until
 * @brief Call @p handler if delimiter @p delim is encountered.
 *
 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
 * @param buffer The buffer that should be used for reading.
 * @param delim A zero terminated string containing the delimiter to be found. Pay attention that the delimiter string
 *              is not copied and must therefore survive until @p handler is called.
 * @param handler The callback function to be called when the read
 * request is fulfilled.
 * @param handler_context A pointer to a context which might be
 * useful inside @p handler
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_buffered_stream_read_until(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler_t handler, void *handler_context);

/**
 * @anchor cio_buffered_stream_at_most
 * @brief Call @p handler with at least 1 byte in @p buffer but at most @p num bytes in buffer.
 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
 * @param buffer The buffer that should be used for reading.
 * @param num The number of bytes the should be read at most.
 * @param handler The callback function to be called when the read
 * request is fulfilled.
 * @param handler_context A pointer to a context which might be
 * useful inside @p handler
 * @return ::CIO_SUCCESS for success..
 */
CIO_EXPORT enum cio_error cio_buffered_stream_read_at_most(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context);

/**
 * @brief Closes the stream.
 *
 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_buffered_stream_close(struct cio_buffered_stream *bs);

/**
 * @brief Writes @p count bytes to the buffered stream.
 *
 * @p handler is called when either the buffer was completely written or an error occured.
 *
 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
 * @param buffer The buffer where the data is written from.
 * @param handler The callback function to be called when the write
 * request is fulfilled.
 * @param handler_context A pointer to a context which might be
 * useful inside @p handler
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_buffered_stream_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buffer, cio_buffered_stream_write_handler_t handler, void *handler_context);

#ifdef __cplusplus
}
#endif

#endif
