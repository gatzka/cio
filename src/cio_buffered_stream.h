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

#include "cio_allocator.h"
#include "cio_error_code.h"
#include "cio_io_stream.h"
#include "cio_write_buffer.h"

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
 * The callback of a @p write call into a buffered_stream is only called
 * if either the provided buffer was completely written or an error occured.
 */

struct cio_buffered_stream;

/**
 * @brief The type of a function passed to all cio_buffered_stream read callback functions.
 * 
 * @param bs The cio_buffered_stream the read operation was called on.
 * @param handler_context The context the functions works on.
 * @param err If err != ::cio_success, the read operation failed.
 * @param buf A pointer to the begin of the buffer where the data was read in. 
 * @param bytes_transferred The number of bytes transferred into @p buf.
 */
typedef void (*cio_buffered_stream_read_handler)(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred);

/**
 * @brief The type of a function passed to all cio_buffered_stream write callback functions.
 * 
 * @param bs The cio_buffered_stream the write operation was called on.
 * @param handler_context The context the functions works on.
 * @param buffer The buffer which should have been written.
 * @param err If err != ::cio_success, the write operation failed.
 * @param bytes_transferred The number of bytes transferred.
 */
typedef void (*cio_buffered_stream_write_handler)(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer_head *buffer, enum cio_error err, size_t bytes_transferred);

/**
 * Interface description for implementing buffered I/O.
 */
struct cio_buffered_stream {

	/**
	 * @brief Call @p handler if exactly @p num bytes are read.
	 *
	 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
	 * @param num The number of bytes to be read when @p handler will be called.
	 * @param handler The callback function to be called when the read
	 * request is fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*read_exactly)(struct cio_buffered_stream *bs, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);

	/**
	 * @brief Read upto @p count bytes into the buffer @p buf starting
	 * with offset @p offset.
	 *
	 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
	 * @param buf The buffer to be filled.
	 * @param offset The start offset in @p buf at which the data is
	 * written.
	 * @param count The maximum number of bytes to read.
	 * @param handler The callback function to be called when the read
	 * request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*read)(struct cio_buffered_stream *bs, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);

	/**
	 * @brief Call @p handler if delimiter @p delim is encountered.
	 *
	 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
	 * @param delim A zero terminated string containing the delimiter to be found. Pay attention that the delimiter string
	 *              is not copied and must therefore survive until @p handler is called.
	 * @param handler The callback function to be called when the read
	 * request is fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*read_until)(struct cio_buffered_stream *bs, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context);

	/**
	 * @brief Writes @p count bytes to the buffered stream.
	 *
	 * @p handler is called when either the buffer was completely written or an error occured.
	 *
	 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
	 * @param buffer The buffer where the data is written from.
	 * @param count The number of to write.
	 * @param handler The callback function to be called when the write
	 * request is fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*write)(struct cio_buffered_stream *bs, struct cio_write_buffer_head *buffer, cio_buffered_stream_write_handler handler, void *handler_context);

	/**
	 * @anchor cio_buffered_stream_close
	 * @brief Closes the stream.
	 *
	 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
	 */
	void (*close)(struct cio_buffered_stream *bs);

	/**
	 * @privatesection
	 */
	struct cio_io_stream *stream;
	size_t read_buffer_size;
	uint8_t *read_buffer;
	uint8_t *read_from_ptr;
	size_t unread_bytes;
	cio_buffered_stream_read_handler read_handler;
	void *read_handler_context;

	void (*read_job)(struct cio_buffered_stream *bs);

	union {
		size_t bytes_to_read;
		struct {
			const char *delim;
			size_t delim_length;
		} until;
	} read_info;

	struct cio_allocator *read_buffer_allocator;

	cio_buffered_stream_write_handler write_handler;
	void *write_handler_context;

	struct cio_write_buffer_head *original_wbh;
	struct cio_write_buffer_head wbh;
	struct cio_write_buffer wb;

	enum cio_error last_error;
};

/**
 * @brief Initializes a cio_buffered_stream.
 *
 * @param bs The cio_buffered_stream that should be initialized.
 * @param stream The IO stream that should be used to read from
 * and to write to.
 * @param read_buffer_size The minimal size of the internal read buffer.
 * @param read_buffer_allocator The allocator that will be used to allocate
 * the memory for internal read buffer. The allocated memory will be freed
 * automatically when calling @ref cio_buffered_stream_close "close" on the
 * cio_buffered_stream.
 * @return ::cio_success for success. ::cio_invalid_argument if either
 * @p read_buffer_allocator or @p write_buffer_allocator is @p NULL.
 */
enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream,
                                        size_t read_buffer_size,
                                        struct cio_allocator *read_buffer_allocator);

#ifdef __cplusplus
}
#endif

#endif
