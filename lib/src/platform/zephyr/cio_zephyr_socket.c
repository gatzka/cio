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

#define CIO_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

struct cio_io_stream *cio_socket_get_io_stream(struct cio_socket *socket)
{
	return &socket->stream;
}

static void tcp_received(struct net_context *context, struct net_pkt *pkt, union net_ip_header *ip_hdr, union net_proto_header *proto_hdr, int status, void *user_data)
{
	context->recv_cb = NULL;

	struct cio_io_stream *stream = user_data;
	struct cio_read_buffer *rb = stream->read_buffer;
	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);

	if ((pkt == NULL) && (status == 0)) {
		socket->impl.peer_closed_connection = true;
		socket->impl.read_status = CIO_EOF;
	} else {
		if (cio_likely(status == 0)) {
			size_t number_of_bytes_in_pkt = net_pkt_remaining_data(pkt);
			size_t available_space_in_buffer = cio_read_buffer_space_available(rb);
			size_t data_to_read = CIO_MIN(number_of_bytes_in_pkt, available_space_in_buffer);
			int ret = net_pkt_read(pkt, rb->add_ptr, data_to_read);
			if (cio_unlikely(ret < 0)) {
				socket->impl.read_status = (enum cio_error)ret;
			} else {
				socket->impl.read_status = CIO_SUCCESS;
				rb->add_ptr += (size_t)data_to_read;
				net_context_update_recv_wnd(context, data_to_read);
			}
		} else {
			socket->impl.read_status = (enum cio_error)status;
		}

		net_pkt_unref(pkt);
	}

	cio_zephyr_eventloop_add_event(socket->impl.loop, &socket->impl.ev);
}

static void read_callback(void *context)
{
	struct cio_io_stream *stream = context;
	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);
	enum cio_error err = socket->impl.read_status;
	stream->read_handler(stream, stream->read_handler_context, err, stream->read_buffer);
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);

	socket->stream.read_buffer = buffer;
	socket->stream.read_handler = handler;
	socket->stream.read_handler_context = handler_context;
	socket->impl.ev.callback = read_callback;
	socket->impl.ev.context = stream;

	int ret = net_context_recv(socket->impl.context, tcp_received, K_NO_WAIT, stream);
	if (cio_unlikely(ret < 0)) {
		return (enum cio_error)ret;
	}

	return CIO_SUCCESS;
}

static void written_callback(void *context)
{
	struct cio_io_stream *stream = context;
	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);
	int status = socket->impl.send_status;

	if (cio_unlikely(status < 0)) {
		stream->write_handler(stream, stream->write_handler_context, stream->write_buffer, (enum cio_error)status, 0);
		return;
	}

	stream->write_handler(stream, stream->write_handler_context, stream->write_buffer, CIO_SUCCESS, socket->impl.bytes_to_send);
}

static void tcp_sent(struct net_context *context, int status, void *user_data)
{
	if (status == 0) {
		// This is an artefact because the zephyr stack immediatly calls the callback
		// in the context of net_context_send() with status == 0
		return;
	}

	struct cio_io_stream *stream = user_data;
	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);
	socket->impl.send_status = status;

	context->send_cb = NULL;
	cio_zephyr_eventloop_add_event(socket->impl.loop, &socket->impl.ev);
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);

	stream->write_handler = handler;
	stream->write_handler_context = handler_context;
	stream->write_buffer = buffer;
	socket->impl.ev.callback = written_callback;
	socket->impl.ev.context = stream;

	size_t chain_length = cio_write_buffer_get_num_buffer_elements(buffer);

	struct iovec msg_iov[chain_length];
	struct msghdr msg;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	msg.msg_iov = msg_iov;
	msg.msg_iovlen = chain_length;

	size_t bytes_to_send = 0;
	struct cio_write_buffer *wb = buffer->next;
	for (size_t i = 0; i < chain_length; i++) {
		msg_iov[i].iov_base = wb->data.element.data;
		msg_iov[i].iov_len = wb->data.element.length;
		bytes_to_send += wb->data.element.length;
		wb = wb->next;
	}

	socket->impl.bytes_to_send = bytes_to_send;

	int ret = net_context_sendmsg(socket->impl.context, &msg, 0, tcp_sent, K_NO_WAIT, stream);
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

enum cio_error cio_zephyr_socket_init(struct cio_socket *socket, struct net_context *net_context, struct cio_eventloop *loop, uint64_t close_timeout_ns, cio_socket_close_hook_t close_hook)
{
	socket->impl.context = net_context;
	socket->impl.close_timeout_ns = close_timeout_ns;

	cio_zephyr_ev_init(&socket->impl.ev);
	socket->impl.ev.callback = NULL;
	socket->impl.ev.context = socket;

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
