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

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_endian.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_inet_socket_address.h"
#include "cio_io_stream.h"
#include "cio_linux_socket.h"
#include "cio_read_buffer.h"
#include "cio_socket.h"
#include "cio_timer.h"
#include "cio_util.h"
#include "cio_write_buffer.h"
#include "linux/cio_linux_socket_utils.h"

static void read_callback(void *context, enum cio_epoll_error error)
{
	struct cio_io_stream *stream = context;
	struct cio_read_buffer *rb = stream->read_buffer;
	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);

	enum cio_error err;
	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		err = cio_linux_get_socket_error(s->impl.ev.fd);
		stream->read_handler(stream, stream->read_handler_context, err, rb);
		return;
	}

	ssize_t ret = read(s->impl.ev.fd, rb->add_ptr, cio_read_buffer_space_available(rb));
	if (ret == -1) {
		if (cio_unlikely(errno != EAGAIN)) {
			stream->read_handler(stream, stream->read_handler_context, (enum cio_error)(-errno), rb);
		}
	} else {
		if (ret == 0) {
			err = CIO_EOF;
			s->impl.peer_closed_connection = true;
		} else {
			rb->add_ptr += (size_t)ret;
			err = CIO_SUCCESS;
		}

		stream->read_handler(stream, stream->read_handler_context, err, rb);
	}
}

static void close_and_call_hook(struct cio_socket *s)
{
	close(s->impl.ev.fd);
	if (s->close_hook != NULL) {
		s->close_hook(s);
	}
}

static void close_socket(struct cio_socket *s)
{
	cio_linux_eventloop_unregister_read(s->impl.loop, &s->impl.ev);
	cio_linux_eventloop_remove(s->impl.loop, &s->impl.ev);

	cio_timer_close(&s->impl.close_timer);
	close_and_call_hook(s);
}

static void reset_connection(struct cio_socket *s)
{
	struct linger linger = {.l_onoff = 1, .l_linger = 0};
	int ret = setsockopt(s->impl.ev.fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));

	// Deliberately ignore the return value of setsockopt.
	// If setsockopt fails, we close the socket anyhow.
	(void)ret;
	close_socket(s);
}

#define READ_CLOSE_BUFFER_SIZE 20

static void cancel_timer_and_reset_connection(struct cio_socket *s)
{
	cio_timer_cancel(&s->impl.close_timer);
	reset_connection(s);
}

static void read_until_close_callback(void *context, enum cio_epoll_error error)
{
	uint8_t buffer[READ_CLOSE_BUFFER_SIZE];

	struct cio_socket *s = (struct cio_socket *)context;

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		cancel_timer_and_reset_connection(s);
		return;
	}

	ssize_t ret = read(s->impl.ev.fd, buffer, sizeof(buffer));
	if (ret == -1) {
		if (cio_unlikely(errno != EAGAIN)) {
			cancel_timer_and_reset_connection(s);
		}

		return;
	}

	if (ret == 0) {
		cio_timer_cancel(&s->impl.close_timer);
		close_socket(s);
	}
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
	s->impl.ev.context = stream;
	s->impl.ev.read_callback = read_callback;
	s->stream.read_buffer = buffer;
	s->stream.read_handler = handler;
	s->stream.read_handler_context = handler_context;
	return cio_linux_eventloop_register_read(s->impl.loop, &s->impl.ev);
}

static void write_callback(void *context, enum cio_epoll_error error)
{
	struct cio_io_stream *stream = context;

	enum cio_error err;
	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
		err = cio_linux_get_socket_error(s->impl.ev.fd);
	} else {
		err = CIO_SUCCESS;
	}

	stream->write_handler(stream, stream->write_handler_context, stream->write_buffer, err, 0);
}

static enum cio_error stream_write(struct cio_io_stream *stream, const struct cio_write_buffer *buffer, cio_io_stream_write_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
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

	struct cio_write_buffer *wb = buffer->next;
	for (size_t i = 0; i < chain_length; i++) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
		msg_iov[i].iov_base = (void *)wb->data.element.const_data;
#pragma GCC diagnostic pop
		msg_iov[i].iov_len = wb->data.element.length;
		wb = wb->next;
	}

	ssize_t ret = sendmsg(s->impl.ev.fd, &msg, MSG_NOSIGNAL);
	if (cio_likely(ret >= 0)) {
		handler(stream, handler_context, buffer, CIO_SUCCESS, (size_t)ret);
	} else {
		if (cio_likely(errno == EAGAIN)) {
			s->stream.write_handler = handler;
			s->stream.write_handler_context = handler_context;
			s->stream.write_buffer = buffer;
			s->impl.ev.context = stream;
			s->impl.ev.write_callback = write_callback;
			return cio_linux_eventloop_register_write(s->impl.loop, &s->impl.ev);
		}

		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
	return cio_socket_close(s);
}

enum cio_error cio_linux_socket_init(struct cio_socket *s, int client_fd,
                                     struct cio_eventloop *loop,
                                     uint64_t close_timeout_ns,
                                     cio_socket_close_hook close_hook)
{
	s->impl.ev.fd = client_fd;
	s->impl.ev.write_callback = NULL;
	s->impl.ev.read_callback = NULL;
	s->impl.ev.context = s;
	s->impl.close_timeout_ns = close_timeout_ns;

	s->impl.peer_closed_connection = false;

	s->stream.read_some = stream_read;
	s->stream.write_some = stream_write;
	s->stream.close = stream_close;

	s->impl.loop = loop;
	s->close_hook = close_hook;

	enum cio_error err = cio_timer_init(&s->impl.close_timer, s->impl.loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	err = cio_linux_eventloop_add(s->impl.loop, &s->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		cio_timer_close(&s->impl.close_timer);
	}

	return err;
}

enum cio_error cio_socket_init(struct cio_socket *socket,
                               enum cio_socket_address_family address_family,
                               struct cio_eventloop *loop,
                               uint64_t close_timeout_ns,
                               cio_socket_close_hook close_hook)
{
	if (cio_unlikely(socket == NULL) || (loop == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	int socket_fd = cio_linux_socket_create(address_family);
	if (cio_unlikely(socket_fd == -1)) {
		return (enum cio_error)(-errno);
	}

	return cio_linux_socket_init(socket, socket_fd, loop, close_timeout_ns, close_hook);
}

static void close_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;

	if (err == CIO_SUCCESS) {
		struct cio_socket *s = (struct cio_socket *)handler_context;
		reset_connection(s);
	}
}

static void shutdown_socket(struct cio_socket *s, uint64_t close_timeout_ns)
{
	int ret = shutdown(s->impl.ev.fd, SHUT_WR);
	if (cio_unlikely(ret == -1)) {
		goto reset_connection;
	}

	cio_linux_eventloop_unregister_read(s->impl.loop, &s->impl.ev);

	s->impl.ev.context = s;
	s->impl.ev.read_callback = read_until_close_callback;
	enum cio_error err = cio_linux_eventloop_register_read(s->impl.loop, &s->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto reset_connection;
	}

	err = cio_timer_expires_from_now(&s->impl.close_timer, close_timeout_ns, close_timeout_handler, s);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto reset_connection;
	}

	return;

reset_connection:
	reset_connection(s);
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

	if (socket->impl.close_timeout_ns > 0) {
		shutdown_socket(socket, socket->impl.close_timeout_ns);
		return CIO_SUCCESS;
	}

	close_socket(socket);
	return CIO_SUCCESS;
}

static void connect_callback(void *context, enum cio_epoll_error error)
{
	struct cio_socket *socket = context;
	enum cio_error err;
	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		err = cio_linux_get_socket_error(socket->impl.ev.fd);
	} else {
		err = CIO_SUCCESS;
	}
	socket->handler(socket, socket->handler_context, err);
}

enum cio_error cio_socket_connect(struct cio_socket *socket, struct cio_inet_socket_address *endpoint, cio_connect_handler handler, void *handler_context)
{
	if (cio_unlikely(socket == NULL) || (endpoint == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	struct sockaddr *addr;
	socklen_t addr_len;
	if (endpoint->inet_address.type == CIO_INET4_ADDRESS) {
		memset(&addr4, 0, sizeof(addr4));
		addr4.sin_family = AF_INET;
		memcpy(&addr4.sin_addr.s_addr, endpoint->inet_address.address.addr4.addr, sizeof(endpoint->inet_address.address.addr4.addr));
		addr4.sin_port = cio_htobe16(endpoint->port);
		addr = (struct sockaddr *)&addr4;
		addr_len = sizeof(addr4);
	} else {
		memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		memcpy(&addr6.sin6_addr, endpoint->inet_address.address.addr6.addr, sizeof(endpoint->inet_address.address.addr6.addr));
		addr6.sin6_port = cio_htobe16(endpoint->port);
		addr = (struct sockaddr *)&addr6;
		addr_len = sizeof(addr6);
	}

	int ret = connect(socket->impl.ev.fd, addr, addr_len);
	if (ret == 0) {
		handler(socket, handler_context, CIO_SUCCESS);
	} else {
		if (cio_likely(errno == EINPROGRESS)) {
			socket->handler = handler;
			socket->handler_context = handler_context;
			socket->impl.ev.context = socket;
			socket->impl.ev.write_callback = connect_callback;
			return cio_linux_eventloop_register_write(socket->impl.loop, &socket->impl.ev);
		}

		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

struct cio_io_stream *cio_socket_get_io_stream(struct cio_socket *socket)
{
	return &socket->stream;
}

enum cio_error cio_socket_set_tcp_no_delay(struct cio_socket *socket, bool on)
{
	int tcp_no_delay = (char)on;

	if (setsockopt(socket->impl.ev.fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
	               sizeof(tcp_no_delay)) < 0) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_socket_set_keep_alive(struct cio_socket *socket, bool on, unsigned int keep_idle_s,
                                         unsigned int keep_intvl_s, unsigned int keep_cnt)
{
	int keep_alive;

	if (on) {
		keep_alive = 1;
		if (setsockopt(socket->impl.ev.fd, SOL_TCP, TCP_KEEPIDLE, &keep_idle_s, sizeof(keep_idle_s)) == -1) {
			return (enum cio_error)(-errno);
		}

		if (setsockopt(socket->impl.ev.fd, SOL_TCP, TCP_KEEPINTVL, &keep_intvl_s, sizeof(keep_intvl_s)) == -1) {
			return (enum cio_error)(-errno);
		}

		if (setsockopt(socket->impl.ev.fd, SOL_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt)) == -1) {
			return (enum cio_error)(-errno);
		}
	} else {
		keep_alive = 0;
	}

	if (setsockopt(socket->impl.ev.fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) == -1) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}
