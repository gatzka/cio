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

#include "cio/buffered_stream.h"
#include "cio/compiler.h"
#include "cio/error_code.h"
#include "cio/io_stream.h"
#include "cio/read_buffer.h"
#include "cio/string.h"
#include "cio/write_buffer.h"

#define CIO_MIN(a, b) ((a) < (b) ? (a) : (b))

static void run_read(struct cio_buffered_stream *buffered_stream);

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer)
{
	(void)stream;
	(void)buffer;

	struct cio_buffered_stream *buffered_stream = handler_context;
	buffered_stream->last_error = err;
	run_read(buffered_stream);
}

static enum cio_bs_state call_handler(struct cio_buffered_stream *buffered_stream, enum cio_error err, struct cio_read_buffer *read_buffer, size_t num_bytes)
{
	buffered_stream->read_job = NULL;
	buffered_stream->callback_is_running++;
	buffered_stream->read_handler(buffered_stream, buffered_stream->read_handler_context, err, read_buffer, num_bytes);
	buffered_stream->callback_is_running--;

	if (buffered_stream->shall_close) {
		buffered_stream->stream->close(buffered_stream->stream);
		return CIO_BS_CLOSED;
	}

	return CIO_BS_OPEN;
}

static void fill_buffer(struct cio_buffered_stream *buffered_stream)
{
	struct cio_read_buffer *read_buffer = buffered_stream->read_buffer;
	if (cio_read_buffer_space_available(read_buffer) == 0) {
		if (cio_unlikely(read_buffer->data == read_buffer->fetch_ptr)) {
			buffered_stream->last_error = CIO_MESSAGE_TOO_LONG;
			buffered_stream->read_job(buffered_stream);
			return;
		}

		size_t unread_bytes = cio_read_buffer_unread_bytes(read_buffer);
		memmove(read_buffer->data, read_buffer->fetch_ptr, unread_bytes);
		read_buffer->fetch_ptr = read_buffer->data;
		read_buffer->add_ptr = read_buffer->data + unread_bytes;
	}

	enum cio_error err = buffered_stream->stream->read_some(buffered_stream->stream, read_buffer, handle_read, buffered_stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		call_handler(buffered_stream, err, read_buffer, 0);
	}
}

static void run_read(struct cio_buffered_stream *buffered_stream)
{
	while (buffered_stream->read_job != NULL) {
		enum cio_bs_state err = buffered_stream->read_job(buffered_stream);
		if (err == CIO_BS_AGAIN) {
			fill_buffer(buffered_stream);
			return;
		}

		if (err == CIO_BS_CLOSED) {
			return;
		}
	}
}

static void start_read(struct cio_buffered_stream *buffered_stream)
{
	if (buffered_stream->callback_is_running == 0) {
		run_read(buffered_stream);
	}
}

static enum cio_bs_state internal_read_max(struct cio_buffered_stream *buffered_stream)
{
	struct cio_read_buffer *read_buffer = buffered_stream->read_buffer;

	if (cio_unlikely(buffered_stream->last_error != CIO_SUCCESS)) {
		return call_handler(buffered_stream, buffered_stream->last_error, read_buffer, 0);
	}

	size_t available = cio_read_buffer_unread_bytes(read_buffer);
	if (available > 0) {
		size_t bytes_to_read = CIO_MIN(available, buffered_stream->read_info.bytes_to_read);
		return call_handler(buffered_stream, CIO_SUCCESS, read_buffer, bytes_to_read);
	}

	return CIO_BS_AGAIN;
}

static enum cio_bs_state internal_read_until(struct cio_buffered_stream *buffered_stream)
{
	struct cio_read_buffer *read_buffer = buffered_stream->read_buffer;

	if (cio_unlikely(buffered_stream->last_error != CIO_SUCCESS)) {
		return call_handler(buffered_stream, buffered_stream->last_error, read_buffer, 0);
	}

	const uint8_t *haystack = read_buffer->fetch_ptr;
	const char *needle = buffered_stream->read_info.until.delim;
	size_t needle_length = buffered_stream->read_info.until.delim_length;
	const uint8_t *found = cio_memmem(haystack, cio_read_buffer_unread_bytes(read_buffer), needle, needle_length);
	if (found != NULL) {
		ptrdiff_t diff = (found + needle_length) - read_buffer->fetch_ptr;
		return call_handler(buffered_stream, CIO_SUCCESS, read_buffer, (size_t)diff);
	}

	return CIO_BS_AGAIN;
}

static enum cio_bs_state internal_read_at_least(struct cio_buffered_stream *buffered_stream)
{
	struct cio_read_buffer *read_buffer = buffered_stream->read_buffer;

	if (cio_unlikely(buffered_stream->last_error != CIO_SUCCESS)) {
		return call_handler(buffered_stream, buffered_stream->last_error, read_buffer, 0);
	}

	size_t available = cio_read_buffer_unread_bytes(read_buffer);
	if (buffered_stream->read_info.bytes_to_read <= available) {
		return call_handler(buffered_stream, CIO_SUCCESS, read_buffer, buffered_stream->read_info.bytes_to_read);
	}

	return CIO_BS_AGAIN;
}

static inline bool buffer_partially_written(const struct cio_write_buffer *write_buffer, size_t bytes_transferred)
{
	return write_buffer->data.element.length > bytes_transferred;
}

static inline bool buffer_is_temp_buffer(const struct cio_buffered_stream *buffered_stream, const struct cio_write_buffer *write_buffer)
{
	return &buffered_stream->write_buffer == write_buffer;
}

static void handle_write(struct cio_io_stream *io_stream, void *handler_context, struct cio_write_buffer *buffer, enum cio_error err, size_t bytes_transferred)
{
	(void)buffer;

	struct cio_buffered_stream *buffered_stream = handler_context;
	if (cio_unlikely(err != CIO_SUCCESS)) {
		struct cio_write_buffer *write_buffer = cio_write_buffer_queue_dequeue(&buffered_stream->wbh);
		while (write_buffer != NULL) {
			if (!buffer_is_temp_buffer(buffered_stream, write_buffer)) {
				cio_write_buffer_queue_tail(buffered_stream->original_wbh, write_buffer);
			}
			write_buffer = cio_write_buffer_queue_dequeue(&buffered_stream->wbh);
		}

		buffered_stream->write_handler(buffered_stream, buffered_stream->write_handler_context, err);
		return;
	}

	struct cio_write_buffer *write_buffer = cio_write_buffer_queue_dequeue(&buffered_stream->wbh);
	while (write_buffer != NULL) {
		if (buffer_partially_written(write_buffer, bytes_transferred)) {
			if (!buffer_is_temp_buffer(buffered_stream, write_buffer)) {
				cio_write_buffer_queue_tail(buffered_stream->original_wbh, write_buffer);
			}

			const void *new_data = &((const uint8_t *)write_buffer->data.element.const_data)[bytes_transferred];
			size_t new_length = write_buffer->data.element.length - bytes_transferred;
			cio_write_buffer_const_element_init(&buffered_stream->write_buffer, new_data, new_length);
			cio_write_buffer_queue_head(&buffered_stream->wbh, &buffered_stream->write_buffer);
			break;
		}

		bytes_transferred -= write_buffer->data.element.length;
		if (!buffer_is_temp_buffer(buffered_stream, write_buffer)) {
			cio_write_buffer_queue_tail(buffered_stream->original_wbh, write_buffer);
		}

		write_buffer = cio_write_buffer_queue_dequeue(&buffered_stream->wbh);
	}

	if (cio_write_buffer_queue_empty(&buffered_stream->wbh)) {
		buffered_stream->write_handler(buffered_stream, buffered_stream->write_handler_context, CIO_SUCCESS);
	} else {
		err = buffered_stream->stream->write_some(io_stream, &buffered_stream->wbh, handle_write, buffered_stream);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			buffered_stream->write_handler(buffered_stream, buffered_stream->write_handler_context, err);
		}
	}
}

static void handle_partial_writes(struct cio_buffered_stream *buffered_stream, struct cio_io_stream *io_stream, struct cio_write_buffer *buffer, size_t bytes_transferred)
{
	cio_write_buffer_head_init(&buffered_stream->wbh);
	buffered_stream->original_wbh = buffer;

	buffer = buffer->next;
	while (!buffer_partially_written(buffer, bytes_transferred)) {
		bytes_transferred -= buffer->data.element.length;
		buffer = buffer->next;
	}

	const void *new_data = &((const uint8_t *)buffer->data.element.const_data)[bytes_transferred];
	size_t new_length = buffer->data.element.length - bytes_transferred;
	cio_write_buffer_const_element_init(&buffered_stream->write_buffer, new_data, new_length);
	cio_write_buffer_queue_head(&buffered_stream->wbh, &buffered_stream->write_buffer);

	cio_write_buffer_split_and_append(&buffered_stream->wbh, buffered_stream->original_wbh, buffer->next);

	enum cio_error err = buffered_stream->stream->write_some(io_stream, &buffered_stream->wbh, handle_write, buffered_stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		buffered_stream->write_handler(buffered_stream, buffered_stream->write_handler_context, err);
	}
}

static void handle_first_write(struct cio_io_stream *io_stream, void *handler_context, struct cio_write_buffer *buffer, enum cio_error err, size_t bytes_transferred)
{
	struct cio_buffered_stream *buffered_stream = handler_context;
	if (cio_unlikely(err != CIO_SUCCESS)) {
		buffered_stream->write_handler(buffered_stream, buffered_stream->write_handler_context, err);
		return;
	}

	if (cio_likely(bytes_transferred == buffer->data.head.total_length)) {
		buffered_stream->write_handler(buffered_stream, buffered_stream->write_handler_context, err);
		return;
	}

	handle_partial_writes(buffered_stream, io_stream, buffer, bytes_transferred);
}

enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *buffered_stream,
                                        struct cio_io_stream *stream)
{
	if (cio_unlikely((buffered_stream == NULL) || (stream == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	buffered_stream->stream = stream;
	buffered_stream->callback_is_running = 0;
	buffered_stream->shall_close = false;

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_read_at_least(struct cio_buffered_stream *buffered_stream, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((buffered_stream == NULL) || (handler == NULL) || (buffer == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely(num > cio_read_buffer_size(buffer))) {
		return CIO_MESSAGE_TOO_LONG;
	}

	buffered_stream->read_info.bytes_to_read = num;
	buffered_stream->read_job = internal_read_at_least;
	buffered_stream->read_buffer = buffer;
	buffered_stream->read_handler = handler;
	buffered_stream->read_handler_context = handler_context;
	buffered_stream->last_error = CIO_SUCCESS;
	start_read(buffered_stream);

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_read_until(struct cio_buffered_stream *buffered_stream, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((buffered_stream == NULL) || (buffer == NULL) || (handler == NULL) || (delim == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	buffered_stream->read_info.until.delim = delim;
	buffered_stream->read_info.until.delim_length = strlen(delim);
	buffered_stream->read_job = internal_read_until;
	buffered_stream->read_buffer = buffer;
	buffered_stream->read_handler = handler;
	buffered_stream->read_handler_context = handler_context;
	buffered_stream->last_error = CIO_SUCCESS;
	start_read(buffered_stream);

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_read_at_most(struct cio_buffered_stream *buffered_stream, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((buffered_stream == NULL) || (handler == NULL) || (buffer == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	buffered_stream->read_info.bytes_to_read = num;
	buffered_stream->read_job = internal_read_max;
	buffered_stream->read_buffer = buffer;
	buffered_stream->read_handler = handler;
	buffered_stream->read_handler_context = handler_context;
	buffered_stream->last_error = CIO_SUCCESS;
	start_read(buffered_stream);

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_close(struct cio_buffered_stream *buffered_stream)
{
	if (cio_unlikely(buffered_stream == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (buffered_stream->callback_is_running == 0) {
		buffered_stream->stream->close(buffered_stream->stream);
	} else {
		buffered_stream->shall_close = true;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_buffered_stream_write(struct cio_buffered_stream *buffered_stream, struct cio_write_buffer *buffer, cio_buffered_stream_write_handler_t handler, void *handler_context)
{
	if (cio_unlikely((buffered_stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	buffered_stream->write_handler = handler;
	buffered_stream->write_handler_context = handler_context;

	return buffered_stream->stream->write_some(buffered_stream->stream, buffer, handle_first_write, buffered_stream);
}
