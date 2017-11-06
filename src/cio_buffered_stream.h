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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cio_error_code.h"
#include "cio_io_stream.h"
#include "cio_read_buffer.h"
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
 * @param buffer The buffer where the data read is stored.
 */
typedef void (*cio_buffered_stream_read_handler)(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer);

/**
 * @brief The type of a function passed to all cio_buffered_stream write callback functions.
 * 
 * @param bs The cio_buffered_stream the write operation was called on.
 * @param handler_context The context the functions works on.
 * @param buffer The buffer which should have been written.
 * @param err If err != ::cio_success, the write operation failed.
 */
typedef void (*cio_buffered_stream_write_handler)(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err);

enum cio_bs_state {
	cio_bs_open = 0,
	cio_bs_closed,
	cio_bs_again
};

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
	 *
	 * @return ::cio_success for success.
	 */
	enum cio_error (*read_exactly)(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);

	/**
	 * @brief Read upto the size of @p buffer.
	 *
	 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
	 * @param buffer The buffer to be filled.
	 * @param handler The callback function to be called when the read
	 * request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 *
	 * @return ::cio_success for success.
	 */
	enum cio_error (*read)(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, cio_buffered_stream_read_handler handler, void *handler_context);

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
	 *
	 * @return ::cio_success for success.
	 */
	enum cio_error (*read_until)(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context);

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
	 *
	 * @return ::cio_success for success.
	 */
	enum cio_error (*write)(struct cio_buffered_stream *bs, struct cio_write_buffer *buffer, cio_buffered_stream_write_handler handler, void *handler_context);

	/**
	 * @anchor cio_buffered_stream_close
	 * @brief Closes the stream.
	 *
	 * @param bs A pointer to the cio_buffered_stream of the on which the operation should be performed.
	 *
	 * @return ::cio_success for success.
	 */
	enum cio_error (*close)(struct cio_buffered_stream *bs);

	/**
	 * @privatesection
	 */
	struct cio_io_stream *stream;

	struct cio_read_buffer *read_buffer;
	cio_buffered_stream_read_handler read_handler;
	void *read_handler_context;

	enum cio_bs_state (*read_job)(struct cio_buffered_stream *bs);

	union {
		size_t bytes_to_read;
		struct {
			const char *delim;
			size_t delim_length;
		} until;
	} read_info;

	cio_buffered_stream_write_handler write_handler;
	void *write_handler_context;

	struct cio_write_buffer *original_wbh;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	enum cio_error last_error;
	bool read_is_running;
	bool shall_close;
};

/**
 * @brief Initializes a cio_buffered_stream.
 *
 * @param bs The cio_buffered_stream that should be initialized.
 * @param stream The IO stream that should be used to read from
 * and to write to.
 *
 * @return ::cio_success for success.
 */
enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream);

#ifdef __cplusplus
}
#endif

#endif
