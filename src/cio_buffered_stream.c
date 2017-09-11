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

#include <string.h>

#include <cio_allocator.h>
#include <cio_buffered_stream.h>
#include <cio_compiler.h>
#include <cio_error_code.h>
#include <cio_io_stream.h>

static inline size_t unread_bytes(const struct cio_buffered_stream *bs)
{
	return bs->unread_bytes;
}

static inline size_t space_in_buffer(const struct cio_buffered_stream *bs)
{
	return bs->read_buffer_size - unread_bytes(bs);
}

static void handle_read(struct cio_io_stream *context, void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred)
{
	(void)context;
	(void)buf;

	struct cio_buffered_stream *bs = handler_context;
	bs->last_error = err;
	if (likely(err == cio_success)) {
		bs->unread_bytes += bytes_transferred;
	}

	bs->read_job(bs);
}

static void fill_buffer(struct cio_buffered_stream *bs)
{
	if (bs->read_buffer != bs->read_from_ptr) {
		memmove(bs->read_buffer, bs->read_from_ptr, unread_bytes(bs));
		bs->read_from_ptr = bs->read_buffer;
	}

	bs->stream->read_some(bs->stream, bs->read_buffer + unread_bytes(bs), space_in_buffer(bs), handle_read, bs);
}

static void bs_read(struct cio_buffered_stream *context, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)context;
	(void)num;
	(void)handler;
	(void)handler_context;
}

static void bs_read_until(struct cio_buffered_stream *context, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)context;
	(void)delim;
	(void)handler;
	(void)handler_context;
}

static void internal_read_exactly(struct cio_buffered_stream *bs)
{
	if (unlikely(bs->last_error != cio_success)) {
		bs->read_handler(bs, bs->read_handler_context, bs->last_error, NULL, 0);
		return;
	}

	if (bs->bytes_to_read <= unread_bytes(bs)) {
		bs->read_handler(bs, bs->read_handler_context, cio_success, bs->read_from_ptr, bs->bytes_to_read);
		bs->read_from_ptr += bs->bytes_to_read;
		bs->unread_bytes -= bs->bytes_to_read;
	} else {
		fill_buffer(bs);
	}
}

static void bs_read_exactly(struct cio_buffered_stream *bs, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (unlikely(num > bs->read_buffer_size)) {
		handler(bs, handler_context, cio_message_too_long, NULL, 0);
		return;
	}

	bs->bytes_to_read = num;
	bs->read_job = internal_read_exactly;
	bs->read_handler = handler;
	bs->read_handler_context = handler_context;
	bs->last_error = cio_success;
	internal_read_exactly(bs);
}

static void bs_write(struct cio_buffered_stream *bs, const void *buf, size_t count, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)bs;
	(void)buf;
	(void)count;
	(void)handler;
	(void)handler_context;
}

static void bs_flush(struct cio_buffered_stream *context)
{
	(void)context;
}

static void bs_close(struct cio_buffered_stream *context)
{
	context->read_buffer_allocator->free(context->read_buffer_allocator, context->read_buffer);
	context->write_buffer_allocator->free(context->write_buffer_allocator, context->write_buffer);
	context->stream->close(context->stream);
}

enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream,
                                        size_t read_buffer_size,
                                        struct cio_allocator *read_buffer_allocator,
                                        size_t write_buffer_size,
                                        struct cio_allocator *write_buffer_allocator)
{
	if (unlikely((read_buffer_allocator == NULL) || (write_buffer_allocator == NULL) || (stream == NULL))) {
		return cio_invalid_argument;
	}

	bs->stream = stream;
	struct cio_buffer read_buffer = read_buffer_allocator->alloc(read_buffer_allocator, read_buffer_size);
	if (unlikely(read_buffer.address == NULL)) {
		return cio_not_enough_memory;
	}

	bs->read_buffer = read_buffer.address;
	bs->read_buffer_size = read_buffer.size;
	bs->read_buffer_allocator = read_buffer_allocator;
	bs->read_from_ptr = read_buffer.address;
	bs->unread_bytes = 0;

	struct cio_buffer write_buffer = write_buffer_allocator->alloc(write_buffer_allocator, write_buffer_size);
	if (unlikely(write_buffer.address == NULL)) {
		read_buffer_allocator->free(read_buffer_allocator, read_buffer.address);
		return cio_not_enough_memory;
	}

	bs->write_buffer = write_buffer.address;
	bs->write_buffer_size = write_buffer.size;
	bs->write_buffer_allocator = write_buffer_allocator;

	bs->read = bs_read;
	bs->read_exactly = bs_read_exactly;
	bs->read_until = bs_read_until;
	bs->write = bs_write;
	bs->flush = bs_flush;
	bs->close = bs_close;

	return cio_success;
}
