/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2020> <Stephan Gatzka>
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

#include <net/net_context.h>
#include <stdint.h>

#include "cio_compiler.h"
#include "cio_endian.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_io_stream.h"
#include "cio_read_buffer.h"
#include "cio_socket.h"
#include "cio_socket_address.h"
#include "cio_timer.h"
#include "cio_util.h"
#include "cio_write_buffer.h"
#include "cio_zephyr_socket.h"

struct cio_io_stream *cio_socket_get_io_stream(struct cio_socket *socket)
{
	return &socket->stream;
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context)
{
	return CIO_SUCCESS;
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler handler, void *handler_context)
{
	return CIO_SUCCESS;
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
	return cio_socket_close(s);
}

static void close_and_call_hook(struct cio_socket *socket)
{
	net_context_put(socket->impl.context);
	if (socket->close_hook != NULL) {
		socket->close_hook(socket);
	}
}

static void close_socket(struct cio_socket *socket)
{
	// TODO: Implement close timeout
	// cio_timer_close(&socket->impl.close_timer);
	close_and_call_hook(socket);
}

enum cio_error cio_socket_close(struct cio_socket *socket)
{
	if (cio_unlikely(socket == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (socket->impl.peer_closed_connection) {
		close_socket(socket);
		return CIO_SUCCESS;
	}

	// TODO
	// if (socket->impl.close_timeout_ns > 0) {
	// 	shutdown_socket(socket, socket->impl.close_timeout_ns);
	// 	return CIO_SUCCESS;
	// }

	close_socket(socket);
	return CIO_SUCCESS;
}

enum cio_error cio_zephyr_socket_init(struct cio_socket *socket, struct net_context *net_context, struct cio_eventloop *loop, uint64_t close_timeout_ns, cio_socket_close_hook close_hook)
{
	socket->impl.context = net_context;
	socket->impl.ev.callback = NULL;
	socket->impl.ev.context = socket;
	socket->impl.close_timeout_ns = close_timeout_ns;

	socket->impl.peer_closed_connection = false;

	socket->stream.read_some = stream_read;
	socket->stream.write_some = stream_write;
	socket->stream.close = stream_close;

	socket->impl.loop = loop;
	socket->close_hook = close_hook;

	// TODO: Implement close timeout
	// enum cio_error err = cio_timer_init(&s->impl.close_timer, s->impl.loop, NULL);
	// if (cio_unlikely(err != CIO_SUCCESS)) {
	// 	return err;
	// }

	return CIO_SUCCESS;
}
