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

#include <stdint.h>
#include <string.h>

#include "cio_buffered_stream.h"
#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_io_stream.h"
#include "cio_read_buffer.h"
#include "cio_string.h"
#include "cio_write_buffer.h"

#undef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	(void)stream;

	struct cio_buffered_stream *bs = handler_context;
	bs->last_error = err;
	if (likely(err == cio_success)) {
		bs->read_buffer->unread_bytes += buffer->bytes_transferred;
	}

	bs->read_job(bs);
}

static void fill_buffer(struct cio_buffered_stream *bs)
{
	if (bs->read_buffer->data != bs->read_buffer->read_from_ptr) {
		memmove(bs->read_buffer->data, bs->read_buffer->read_from_ptr, cio_read_buffer_unread_bytes(bs->read_buffer));
		bs->read_buffer->read_from_ptr = bs->read_buffer->data;
	}

	cio_read_buffer_init(&bs->read_some_buffer, bs->read_buffer->data + cio_read_buffer_unread_bytes(bs->read_buffer), cio_read_buffer_space_available(bs->read_buffer));
	bs->stream->read_some(bs->stream, &bs->read_some_buffer, handle_read, bs);
}

static void internal_read(struct cio_buffered_stream *bs)
{
	size_t available = cio_read_buffer_unread_bytes(bs->read_buffer);
	if (available > 0) {
		size_t to_read = MIN(available, bs->read_info.bytes_to_read);
		bs->read_buffer->bytes_transferred = to_read;
		bs->read_buffer->read_from_ptr += to_read;
		bs->read_buffer->unread_bytes -= to_read;
		bs->read_handler(bs, bs->read_handler_context, cio_success, bs->read_buffer);
	} else {
		fill_buffer(bs);
	}
}

static enum cio_error bs_read(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (unlikely((bs == NULL) || (buffer == NULL) || (handler == NULL))) {
		return cio_invalid_argument;
	}

	bs->read_info.bytes_to_read = buffer->size;
	bs->read_job = internal_read;
	bs->read_buffer = buffer;
	bs->read_handler = handler;
	bs->read_handler_context = handler_context;
	bs->last_error = cio_success;
	internal_read(bs);

	return cio_success;
}

static void internal_read_until(struct cio_buffered_stream *bs)
{
	const uint8_t *haystack = bs->read_buffer->read_from_ptr;
	const char *needle = bs->read_info.until.delim;
	size_t needle_length = bs->read_info.until.delim_length;
	uint8_t *found = cio_memmem(haystack, cio_read_buffer_unread_bytes(bs->read_buffer), needle, needle_length);
	if (found != NULL) {
		ptrdiff_t diff = (found + needle_length) - bs->read_buffer->read_from_ptr;
		bs->read_buffer->bytes_transferred = diff;
		bs->read_buffer->read_from_ptr += diff;
		bs->read_buffer->unread_bytes -= diff;
		bs->read_handler(bs, bs->read_handler_context, cio_success, bs->read_buffer);
	} else {
		fill_buffer(bs);
	}
}

static enum cio_error bs_read_until(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (unlikely((bs == NULL) || (buffer == NULL) || (handler == NULL) || (delim == NULL))) {
		return cio_invalid_argument;
	}

	bs->read_info.until.delim = delim;
	bs->read_info.until.delim_length = strlen(delim);
	bs->read_job = internal_read_until;
	bs->read_buffer = buffer;
	bs->read_handler = handler;
	bs->read_handler_context = handler_context;
	bs->last_error = cio_success;
	internal_read_until(bs);

	return cio_success;
}

static void internal_read_exactly(struct cio_buffered_stream *bs)
{
	if (unlikely(bs->last_error != cio_success)) {
		bs->read_handler(bs, bs->read_handler_context, bs->last_error, NULL);
		return;
	}

	if (bs->read_info.bytes_to_read <= cio_read_buffer_unread_bytes(bs->read_buffer)) {
		bs->read_buffer->bytes_transferred = bs->read_info.bytes_to_read;
		bs->read_buffer->read_from_ptr += bs->read_info.bytes_to_read;
		bs->read_buffer->unread_bytes -= bs->read_info.bytes_to_read;
		bs->read_handler(bs, bs->read_handler_context, cio_success, bs->read_buffer);
	} else {
		fill_buffer(bs);
	}
}

static enum cio_error bs_read_exactly(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (unlikely((bs == NULL) || (handler == NULL) || (buffer == NULL))) {
		return cio_invalid_argument;
	}

	if (unlikely(num > buffer->size)) {
		return cio_message_too_long;
	}

	bs->read_info.bytes_to_read = num;
	bs->read_job = internal_read_exactly;
	bs->read_buffer = buffer;
	bs->read_handler = handler;
	bs->read_handler_context = handler_context;
	bs->last_error = cio_success;
	internal_read_exactly(bs);

	return cio_success;
}

static inline bool buffer_partially_written(const struct cio_write_buffer *wb, size_t bytes_transferred)
{
	return wb->data.element.length > bytes_transferred;
}

static inline bool buffer_is_temp_buffer(const struct cio_buffered_stream *bs, const struct cio_write_buffer *wb)
{
	return &bs->wb == wb;
}

static void handle_write(struct cio_io_stream *io_stream, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err, size_t bytes_transferred)
{
	(void)buffer;

	struct cio_buffered_stream *bs = handler_context;
	if (unlikely(err != cio_success)) {
		while (!cio_write_buffer_queue_empty(&bs->wbh)) {
			struct cio_write_buffer *wb = cio_write_buffer_queue_dequeue(&bs->wbh);
			if (!buffer_is_temp_buffer(bs, wb)) {
				cio_write_buffer_queue_tail(bs->original_wbh, wb);
			}
		}

		bs->write_handler(bs, bs->write_handler_context, bs->original_wbh, err);
		return;
	}

	while (bytes_transferred != 0) {
		struct cio_write_buffer *wb = cio_write_buffer_queue_dequeue(&bs->wbh);
		if (buffer_partially_written(wb, bytes_transferred)) {
			if (!buffer_is_temp_buffer(bs, wb)) {
				cio_write_buffer_queue_tail(bs->original_wbh, wb);
			}

			const void *new_data = &((const uint8_t *)wb->data.element.data)[bytes_transferred];
			size_t new_length = wb->data.element.length - bytes_transferred;
			cio_write_buffer_init(&bs->wb, new_data, new_length);
			cio_write_buffer_queue_head(&bs->wbh, &bs->wb);
			bytes_transferred = 0;
		} else {
			bytes_transferred -= wb->data.element.length;
			if (!buffer_is_temp_buffer(bs, wb)) {
				cio_write_buffer_queue_tail(bs->original_wbh, wb);
			}
		}
	}

	if (cio_write_buffer_queue_empty(&bs->wbh)) {
		bs->write_handler(bs, bs->write_handler_context, bs->original_wbh, cio_success);
	} else {
		bs->stream->write_some(io_stream, &bs->wbh, handle_write, bs);
	}
}

static enum cio_error bs_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buffer, cio_buffered_stream_write_handler handler, void *handler_context)
{
	if (unlikely((bs == NULL) || (buffer == NULL) || (handler == NULL))) {
		return cio_invalid_argument;
	}

	bs->write_handler = handler;
	bs->write_handler_context = handler_context;
	bs->original_wbh = buffer;

	while (!cio_write_buffer_queue_empty(buffer)) {
		struct cio_write_buffer *wb = cio_write_buffer_queue_dequeue(buffer);
		cio_write_buffer_queue_tail(&bs->wbh, wb);
	}

	bs->stream->write_some(bs->stream, &bs->wbh, handle_write, bs);

	return cio_success;
}

static enum cio_error bs_close(struct cio_buffered_stream *bs)
{
	if (unlikely(bs == NULL)) {
		return cio_invalid_argument;
	}

	bs->stream->close(bs->stream);

	return cio_success;
}

enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream)
{
	if (unlikely((bs == NULL) || (stream == NULL))) {
		return cio_invalid_argument;
	}

	bs->stream = stream;

	bs->read = bs_read;
	bs->read_exactly = bs_read_exactly;
	bs->read_until = bs_read_until;

	cio_write_buffer_head_init(&bs->wbh);
	bs->write = bs_write;

	bs->close = bs_close;

	return cio_success;
}
