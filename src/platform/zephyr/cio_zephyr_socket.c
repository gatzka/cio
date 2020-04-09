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
#include <net/net_pkt.h>
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

#ifndef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#endif

struct cio_io_stream *cio_socket_get_io_stream(struct cio_socket *socket)
{
	return &socket->stream;
}

static void tcp_received(struct net_context *context, struct net_pkt *pkt, union net_ip_header *ip_hdr, union net_proto_header *proto_hdr, int status, void *user_data)
{
	struct cio_io_stream *stream = user_data;
	struct cio_read_buffer *rb = stream->read_buffer;
	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);

	if ((pkt == NULL) && (status == 0)) {
		s->impl.peer_closed_connection = true;
		stream->read_handler(stream, stream->read_handler_context, CIO_EOF, rb);
		return;
	}

	enum cio_error err;
	if (cio_likely(status == 0)) {
		size_t number_of_bytes_in_pkt = net_pkt_remaining_data(pkt);
		size_t available_space_in_buffer = cio_read_buffer_space_available(rb);
		size_t data_to_read = MIN(number_of_bytes_in_pkt, available_space_in_buffer);
		int ret = net_pkt_read(pkt, rb->add_ptr, data_to_read);
		if (cio_unlikely(ret < 0)) {
			err = (enum cio_error)ret;
		} else {
			err = CIO_SUCCESS;
			rb->add_ptr += (size_t)data_to_read;
			net_context_update_recv_wnd(context, data_to_read);
		}
	} else {
		err = (enum cio_error)status;
	}

	net_pkt_unref(pkt);
	stream->read_handler(stream, stream->read_handler_context, err, rb);
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);

	socket->stream.read_buffer = buffer;
	socket->stream.read_handler = handler;
	socket->stream.read_handler_context = handler_context;

	int ret = net_context_recv(socket->impl.context, tcp_received, K_NO_WAIT, stream);
	if (cio_unlikely(ret < 0)) {
		return (enum cio_error)ret;
	}

	return CIO_SUCCESS;
}

#include <stdio.h>
static void tcp_sent(struct net_context *context, int status, void *user_data)
{
	if (status == 0) {
		// This is an artefact because the zephyr stack immediatly calls the callback
		// in the context of net_context_send() with status == 0
		return;
	}

	struct cio_io_stream *stream = user_data;
	if (cio_unlikely(status < 0)) {
		stream->write_handler(stream, stream->write_handler_context, stream->write_buffer, (enum cio_error)status, 0);
		return;
	}

	fprintf(stderr, "bytes were sent! %d\n", status);
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	size_t chain_length = cio_write_buffer_get_num_buffer_elements(buffer);

	size_t bytes_to_send = 0;
	struct cio_write_buffer *wb = buffer->next;
	for (size_t i = 0; i < chain_length; i++) {
		bytes_to_send += wb->data.element.length;
		wb = wb->next;
	}

	void *send_buffer = alloca(bytes_to_send);
	if (cio_unlikely(send_buffer == NULL)) {
		return CIO_NO_MEMORY;
	}

	wb = buffer->next;
	char *ptr = send_buffer;
	for (size_t i = 0; i < chain_length; i++) {
		memcpy(ptr, wb->data.element.data, wb->data.element.length);
		ptr += wb->data.element.length;
		wb = wb->next;
	}

	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);
	int ret = net_context_send(socket->impl.context, send_buffer, bytes_to_send, tcp_sent, K_NO_WAIT, stream);
	if (cio_unlikely(ret < 0)) {
		return (enum cio_error)ret;
	}

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
