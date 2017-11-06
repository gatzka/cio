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

static enum cio_bs_state run_read(struct cio_buffered_stream *bs);

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	(void)stream;
	(void)buffer;

	struct cio_buffered_stream *bs = handler_context;
	bs->last_error = err;
	run_read(bs);
}

static enum cio_bs_state fill_buffer(struct cio_buffered_stream *bs)
{
	struct cio_read_buffer *rb = bs->read_buffer;
	if (cio_read_buffer_space_available(rb) == 0) {
		if (unlikely(rb->data == rb->fetch_ptr)) {
			bs->last_error = cio_message_too_long;
			return bs->read_job(bs);
		}

		size_t unread_bytes = cio_read_buffer_unread_bytes(rb);
		memmove(rb->data, rb->fetch_ptr, unread_bytes);
		rb->fetch_ptr = rb->data;
		rb->add_ptr = rb->data + unread_bytes;
	}

	bs->stream->read_some(bs->stream, rb, handle_read, bs);
	return cio_bs_open;
}

static enum cio_error bs_close(struct cio_buffered_stream *bs)
{
	if (unlikely(bs == NULL)) {
		return cio_invalid_argument;
	}

	if (!bs->read_is_running) {
		bs->stream->close(bs->stream);
	} else {
		bs->shall_close = true;
	}

	return cio_success;
}

static enum cio_bs_state run_read(struct cio_buffered_stream *bs)
{
	bs->read_is_running = true;
	while (bs->read_job != NULL) {
		enum cio_bs_state err = bs->read_job(bs);
		if (err == cio_bs_again) {
			bs->read_is_running = false;
			return fill_buffer(bs);
		} else if (err == cio_bs_closed) {
			return cio_bs_closed;
		}
	}

	bs->read_is_running = false;

	return cio_bs_open;
}

static void start_read(struct cio_buffered_stream *bs)
{
	if (!bs->read_is_running) {
		run_read(bs);
	}
}

static enum cio_bs_state call_handler(struct cio_buffered_stream *bs, enum cio_error err, struct cio_read_buffer *rb)
{
	bs->read_handler(bs, bs->read_handler_context, err, rb);

	if (bs->shall_close) {
		bs->stream->close(bs->stream);
		return cio_bs_closed;
	} else {
		return cio_bs_open;
	}
}

static enum cio_bs_state internal_read(struct cio_buffered_stream *bs)
{
	struct cio_read_buffer *rb = bs->read_buffer;

	if (unlikely(bs->last_error != cio_success)) {
		bs->read_job = NULL;
		return call_handler(bs, bs->last_error, rb);
	}

	size_t available = cio_read_buffer_unread_bytes(rb);
	if (available > 0) {
		rb->bytes_transferred = available;
		rb->fetch_ptr += available;
		bs->read_job = NULL;
		return call_handler(bs, cio_success, rb);
	} else {
		return cio_bs_again;
	}

	return cio_bs_open;
}

static enum cio_error bs_read(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (unlikely((bs == NULL) || (buffer == NULL) || (handler == NULL))) {
		return cio_invalid_argument;
	}

	bs->read_job = internal_read;
	bs->read_buffer = buffer;
	bs->read_handler = handler;
	bs->read_handler_context = handler_context;
	bs->last_error = cio_success;
	start_read(bs);

	return cio_success;
}

static enum cio_bs_state internal_read_until(struct cio_buffered_stream *bs)
{
	struct cio_read_buffer *rb = bs->read_buffer;

	if (unlikely(bs->last_error != cio_success)) {
		bs->read_job = NULL;
		return call_handler(bs, bs->last_error, rb);
	}

	const uint8_t *haystack = rb->fetch_ptr;
	const char *needle = bs->read_info.until.delim;
	size_t needle_length = bs->read_info.until.delim_length;
	uint8_t *found = cio_memmem(haystack, cio_read_buffer_unread_bytes(rb), needle, needle_length);
	if (found != NULL) {
		ptrdiff_t diff = (found + needle_length) - rb->fetch_ptr;
		rb->bytes_transferred = diff;
		rb->fetch_ptr += diff;
		bs->read_job = NULL;
		return call_handler(bs, cio_success, rb);
	} else {
		return cio_bs_again;
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
	start_read(bs);

	return cio_success;
}

static enum cio_bs_state internal_read_exactly(struct cio_buffered_stream *bs)
{
	struct cio_read_buffer *rb = bs->read_buffer;

	if (unlikely(bs->last_error != cio_success)) {
		bs->read_job = NULL;
		return call_handler(bs, bs->last_error, rb);
	}

	if (bs->read_info.bytes_to_read <= cio_read_buffer_unread_bytes(rb)) {
		rb->bytes_transferred = bs->read_info.bytes_to_read;
		rb->fetch_ptr += bs->read_info.bytes_to_read;
		bs->read_job = NULL;
		return call_handler(bs, cio_success, rb);
	} else {
		return cio_bs_again;
	}
}

static enum cio_error bs_read_exactly(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (unlikely((bs == NULL) || (handler == NULL) || (buffer == NULL))) {
		return cio_invalid_argument;
	}

	if (unlikely(num > cio_read_buffer_size(buffer))) {
		return cio_message_too_long;
	}

	bs->read_info.bytes_to_read = num;
	bs->read_job = internal_read_exactly;
	bs->read_buffer = buffer;
	bs->read_handler = handler;
	bs->read_handler_context = handler_context;
	bs->last_error = cio_success;
	start_read(bs);

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

enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream)
{
	if (unlikely((bs == NULL) || (stream == NULL))) {
		return cio_invalid_argument;
	}

	bs->stream = stream;
	bs->read_is_running = false;
	bs->shall_close = false;

	bs->read = bs_read;
	bs->read_exactly = bs_read_exactly;
	bs->read_until = bs_read_until;

	cio_write_buffer_head_init(&bs->wbh);
	bs->write = bs_write;

	bs->close = bs_close;

	return cio_success;
}
