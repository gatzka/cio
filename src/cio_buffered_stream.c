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

#include <stdint.h>
#include <string.h>

#include "cio_buffered_stream.h"
#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_io_stream.h"
#include "cio_read_buffer.h"
#include "cio_string.h"
#include "cio_write_buffer.h"

static void run_read(struct cio_buffered_stream *bs);

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	(void)stream;
	(void)buffer;

	struct cio_buffered_stream *bs = handler_context;
	bs->last_error = err;
	run_read(bs);
}

static enum cio_bs_state call_handler(struct cio_buffered_stream *bs, enum cio_error err, struct cio_read_buffer *rb, size_t num_bytes)
{
	bs->read_job = NULL;
	bs->callback_is_running++;
	bs->read_handler(bs, bs->read_handler_context, err, rb, num_bytes);
	bs->callback_is_running--;

	if (bs->shall_close) {
		bs->stream->close(bs->stream);
		return CIO_BS_CLOSED;
	}

	return CIO_BS_OPEN;
}

static void fill_buffer(struct cio_buffered_stream *bs)
{
	struct cio_read_buffer *rb = bs->read_buffer;
	if (cio_read_buffer_space_available(rb) == 0) {
		if (cio_unlikely(rb->data == rb->fetch_ptr)) {
			bs->last_error = CIO_MESSAGE_TOO_LONG;
			bs->read_job(bs);
			return;
		}

		size_t unread_bytes = cio_read_buffer_unread_bytes(rb);
		memmove(rb->data, rb->fetch_ptr, unread_bytes);
		rb->fetch_ptr = rb->data;
		rb->add_ptr = rb->data + unread_bytes;
	}

	enum cio_error err = bs->stream->read_some(bs->stream, rb, handle_read, bs);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		call_handler(bs, err, rb, 0);
	}
}

static void run_read(struct cio_buffered_stream *bs)
{
	while (bs->read_job != NULL) {
		enum cio_bs_state err = bs->read_job(bs);
		if (err == CIO_BS_AGAIN) {
			fill_buffer(bs);
			return;
		}

		if (err == CIO_BS_CLOSED) {
			return;
		}
	}
}

static void start_read(struct cio_buffered_stream *bs)
{
	if (bs->callback_is_running == 0) {
		run_read(bs);
	}
}

static enum cio_bs_state internal_read_until(struct cio_buffered_stream *bs)
{
	struct cio_read_buffer *rb = bs->read_buffer;

	if (cio_unlikely(bs->last_error != CIO_SUCCESS)) {
		return call_handler(bs, bs->last_error, rb, 0);
	}

	const uint8_t *haystack = rb->fetch_ptr;
	const char *needle = bs->read_info.until.delim;
	size_t needle_length = bs->read_info.until.delim_length;
	uint8_t *found = cio_memmem(haystack, cio_read_buffer_unread_bytes(rb), needle, needle_length);
	if (found != NULL) {
		ptrdiff_t diff = (found + needle_length) - rb->fetch_ptr;
		return call_handler(bs, CIO_SUCCESS, rb, (size_t)diff);
	}

	return CIO_BS_AGAIN;
}

static enum cio_bs_state internal_read_at_least(struct cio_buffered_stream *bs)
{
	struct cio_read_buffer *rb = bs->read_buffer;

	if (cio_unlikely(bs->last_error != CIO_SUCCESS)) {
		return call_handler(bs, bs->last_error, rb, 0);
	}

	size_t available = cio_read_buffer_unread_bytes(rb);
	if (bs->read_info.bytes_to_read <= available) {
		return call_handler(bs, CIO_SUCCESS, rb, bs->read_info.bytes_to_read);
	}

	return CIO_BS_AGAIN;
}

static inline bool buffer_partially_written(const struct cio_write_buffer *wb, size_t bytes_transferred)
{
	return wb->data.element.length > bytes_transferred;
}

static inline bool buffer_is_temp_buffer(const struct cio_buffered_stream *bs, const struct cio_write_buffer *wb)
{
	return &bs->wb == wb;
}

static void handle_write(struct cio_io_stream *io_stream, void *handler_context, struct cio_write_buffer *buffer, enum cio_error err, size_t bytes_transferred)
{
	(void)buffer;

	struct cio_buffered_stream *bs = handler_context;
	if (cio_unlikely(err != CIO_SUCCESS)) {
		while (!cio_write_buffer_queue_empty(&bs->wbh)) {
			struct cio_write_buffer *wb = cio_write_buffer_queue_dequeue(&bs->wbh);
			if (!buffer_is_temp_buffer(bs, wb)) {
				cio_write_buffer_queue_tail(bs->original_wbh, wb);
			}
		}

		bs->write_handler(bs, bs->write_handler_context, err);
		return;
	}

	while (!cio_write_buffer_queue_empty(&bs->wbh)) {
		struct cio_write_buffer *wb = cio_write_buffer_queue_dequeue(&bs->wbh);
		if (buffer_partially_written(wb, bytes_transferred)) {
			if (!buffer_is_temp_buffer(bs, wb)) {
				cio_write_buffer_queue_tail(bs->original_wbh, wb);
			}

			const void *new_data = &((const uint8_t *)wb->data.element.const_data)[bytes_transferred];
			size_t new_length = wb->data.element.length - bytes_transferred;
			cio_write_buffer_const_element_init(&bs->wb, new_data, new_length);
			cio_write_buffer_queue_head(&bs->wbh, &bs->wb);
			break;
		}

		bytes_transferred -= wb->data.element.length;
		if (!buffer_is_temp_buffer(bs, wb)) {
			cio_write_buffer_queue_tail(bs->original_wbh, wb);
		}
	}

	if (cio_write_buffer_queue_empty(&bs->wbh)) {
		bs->write_handler(bs, bs->write_handler_context, CIO_SUCCESS);
	} else {
		err = bs->stream->write_some(io_stream, &bs->wbh, handle_write, bs);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			bs->write_handler(bs, bs->write_handler_context, err);
		}
	}
}

static void handle_partial_writes(struct cio_buffered_stream *bs, struct cio_io_stream *io_stream, struct cio_write_buffer *buffer, size_t bytes_transferred)
{
	cio_write_buffer_head_init(&bs->wbh);
	bs->original_wbh = buffer;

	buffer = buffer->next;
	while (!buffer_partially_written(buffer, bytes_transferred)) {
		bytes_transferred -= buffer->data.element.length;
		buffer = buffer->next;
	}

	const void *new_data = &((const uint8_t *)buffer->data.element.const_data)[bytes_transferred];
	size_t new_length = buffer->data.element.length - bytes_transferred;
	cio_write_buffer_const_element_init(&bs->wb, new_data, new_length);
	cio_write_buffer_queue_head(&bs->wbh, &bs->wb);

	cio_write_buffer_split_and_append(&bs->wbh, bs->original_wbh, buffer->next);

	enum cio_error err = bs->stream->write_some(io_stream, &bs->wbh, handle_write, bs);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		bs->write_handler(bs, bs->write_handler_context, err);
	}
}

static void handle_first_write(struct cio_io_stream *io_stream, void *handler_context, struct cio_write_buffer *buffer, enum cio_error err, size_t bytes_transferred)
{
	struct cio_buffered_stream *bs = handler_context;
	if (cio_unlikely(err != CIO_SUCCESS)) {
		bs->write_handler(bs, bs->write_handler_context, err);
		return;
	}

	if (cio_likely(bytes_transferred == buffer->data.head.total_length)) {
		bs->write_handler(bs, bs->write_handler_context, err);
		return;
	}

	handle_partial_writes(bs, io_stream, buffer, bytes_transferred);
}

enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream)
{
	if (cio_unlikely((bs == NULL) || (stream == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	bs->stream = stream;
	bs->callback_is_running = 0;
	bs->shall_close = false;

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_read_at_least(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (cio_unlikely((bs == NULL) || (handler == NULL) || (buffer == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely(num > cio_read_buffer_size(buffer))) {
		return CIO_MESSAGE_TOO_LONG;
	}

	bs->read_info.bytes_to_read = num;
	bs->read_job = internal_read_at_least;
	bs->read_buffer = buffer;
	bs->read_handler = handler;
	bs->read_handler_context = handler_context;
	bs->last_error = CIO_SUCCESS;
	start_read(bs);

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_read_until(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (cio_unlikely((bs == NULL) || (buffer == NULL) || (handler == NULL) || (delim == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	bs->read_info.until.delim = delim;
	bs->read_info.until.delim_length = strlen(delim);
	bs->read_job = internal_read_until;
	bs->read_buffer = buffer;
	bs->read_handler = handler;
	bs->read_handler_context = handler_context;
	bs->last_error = CIO_SUCCESS;
	start_read(bs);

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_close(struct cio_buffered_stream *bs)
{
	if (cio_unlikely(bs == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (bs->callback_is_running == 0) {
		bs->stream->close(bs->stream);
	} else {
		bs->shall_close = true;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buffer, cio_buffered_stream_write_handler handler, void *handler_context)
{
	if (cio_unlikely((bs == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	bs->write_handler = handler;
	bs->write_handler_context = handler_context;

	return bs->stream->write_some(bs->stream, buffer, handle_first_write, bs);
}
