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

#include <cio_allocator.h>
#include <cio_compiler.h>
#include <cio_buffered_stream.h>
#include <cio_error_code.h>
#include <cio_io_stream.h>

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

static void bs_read_exactly(struct cio_buffered_stream *context, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)context;
	(void)num;
	(void)handler;
	(void)handler_context;
}

static void bs_write(struct cio_buffered_stream *context, const void *buf, size_t count, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)context;
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
	(void)context;
}

enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream,
                                        size_t read_buffer_size,
                                        struct cio_allocator *read_buffer_allocator,
                                        size_t write_buffer_size,
                                        struct cio_allocator *write_buffer_allocator)
{
	if (unlikely((read_buffer_allocator == NULL) || (write_buffer_allocator == NULL))) {
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
