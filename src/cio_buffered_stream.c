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
#include <cio_buffered_stream.h>
#include <cio_error_code.h>
#include <cio_io_stream.h>

enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream,
                                        size_t read_buffer_size,
                                        struct cio_allocator *read_buffer_allocator,
                                        size_t write_buffer_size,
                                        struct cio_allocator *write_buffer_allocator)
{
	if ((read_buffer_allocator == NULL) || (write_buffer_allocator == NULL)) {
		return cio_invalid_argument;
	}

	bs->stream = stream;
	bs->read_buffer_size = read_buffer_size;
	bs->read_buffer_allocator = read_buffer_allocator;
	bs->write_buffer_size = write_buffer_size;
	bs->write_buffer_allocator = write_buffer_allocator;
	return cio_success;
}
