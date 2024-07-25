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

#include "cio/address_family.h"
#include "cio/compiler.h"
#include "cio/endian.h"
#include "cio/error_code.h"
#include "cio/eventloop_impl.h"
#include "cio/io_stream.h"
#include "cio/linux_socket.h"
#include "cio/linux_socket_utils.h"
#include "cio/read_buffer.h"
#include "cio/socket.h"
#include "cio/socket_address.h"
#include "cio/timer.h"
#include "cio/util.h"
#include "cio/write_buffer.h"

#ifndef TCP_FASTOPEN_CONNECT
#define TCP_FASTOPEN_CONNECT 30 // Define it for older kernels (pre 4.11)
#endif

static void read_callback(void *context, enum cio_epoll_error error)
{
	struct cio_io_stream *stream = context;
	struct cio_read_buffer *read_buffer = stream->read_buffer;
	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);

	enum cio_error err = cio_linux_eventloop_unregister_read(socket->impl.loop, &socket->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		stream->read_handler(stream, stream->read_handler_context, err, read_buffer);
		return;
	}

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		err = cio_linux_get_socket_error(socket->impl.ev.fd);
		stream->read_handler(stream, stream->read_handler_context, err, read_buffer);
		return;
	}

	ssize_t ret = read(socket->impl.ev.fd, read_buffer->add_ptr, cio_read_buffer_space_available(read_buffer));
	if (ret == -1) {
		if (cio_unlikely(errno != EAGAIN)) {
			stream->read_handler(stream, stream->read_handler_context, (enum cio_error)(-errno), read_buffer);
		}
	} else {
		if (ret == 0) {
			err = CIO_EOF;
			socket->impl.peer_closed_connection = true;
		} else {
			read_buffer->add_ptr += (size_t)ret;
		}

		stream->read_handler(stream, stream->read_handler_context, err, read_buffer);
	}
}

static void close_and_call_hook(struct cio_socket *socket)
{
	close(socket->impl.ev.fd);
	if (socket->close_hook != NULL) {
		socket->close_hook(socket);
	}
}

static void close_socket(struct cio_socket *socket)
{
	cio_linux_eventloop_unregister_read(socket->impl.loop, &socket->impl.ev);
	cio_linux_eventloop_remove(socket->impl.loop, &socket->impl.ev);

	cio_timer_close(&socket->impl.close_timer);
	close_and_call_hook(socket);
}

static void reset_connection(struct cio_socket *socket)
{
	struct linger linger = {.l_onoff = 1, .l_linger = 0};
	int ret = setsockopt(socket->impl.ev.fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));

	// Deliberately ignore the return value of setsockopt.
	// If setsockopt fails, we close the socket anyhow.
	(void)ret;
	close_socket(socket);
}

static void cancel_timer_and_reset_connection(struct cio_socket *socket)
{
	cio_timer_cancel(&socket->impl.close_timer);
	reset_connection(socket);
}

enum { READ_CLOSE_BUFFER_SIZE = 20 };

static void read_until_close_callback(void *context, enum cio_epoll_error error)
{
	uint8_t buffer[READ_CLOSE_BUFFER_SIZE];

	struct cio_socket *socket = (struct cio_socket *)context;

	enum cio_error err = cio_linux_eventloop_unregister_read(socket->impl.loop, &socket->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		cancel_timer_and_reset_connection(socket);
		return;
	}

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		cancel_timer_and_reset_connection(socket);
		return;
	}

	ssize_t ret = read(socket->impl.ev.fd, buffer, sizeof(buffer));
	if (ret == -1) {
		if (cio_unlikely(errno != EAGAIN)) {
			cancel_timer_and_reset_connection(socket);
		}

		return;
	}

	if (ret == 0) {
		cio_timer_cancel(&socket->impl.close_timer);
		close_socket(socket);
	}
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);
	socket->impl.ev.context = stream;
	socket->impl.ev.read_callback = read_callback;
	socket->stream.read_buffer = buffer;
	socket->stream.read_handler = handler;
	socket->stream.read_handler_context = handler_context;

	return cio_linux_eventloop_register_read(socket->impl.loop, &socket->impl.ev);
}

static void write_callback(void *context, enum cio_epoll_error error)
{
	struct cio_io_stream *stream = context;

	enum cio_error err = CIO_SUCCESS;

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		const struct cio_socket *socket = cio_const_container_of(stream, struct cio_socket, stream);
		err = cio_linux_get_socket_error(socket->impl.ev.fd);
	}

	stream->write_handler(stream, stream->write_handler_context, stream->write_buffer, err, 0);
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);
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

	const struct cio_write_buffer *write_buffer = buffer->next;
	for (size_t i = 0; i < chain_length; i++) {
		msg_iov[i].iov_base = write_buffer->data.element.data;
		msg_iov[i].iov_len = write_buffer->data.element.length;
		write_buffer = write_buffer->next;
	}

	ssize_t ret = sendmsg(socket->impl.ev.fd, &msg, MSG_NOSIGNAL);
	if (cio_likely(ret >= 0)) {
		handler(stream, handler_context, buffer, CIO_SUCCESS, (size_t)ret);
	} else {
		if (cio_likely(errno == EAGAIN)) {
			socket->stream.write_handler = handler;
			socket->stream.write_handler_context = handler_context;
			socket->stream.write_buffer = buffer;
			socket->impl.ev.context = stream;
			socket->impl.ev.write_callback = write_callback;
			return cio_linux_eventloop_register_write(socket->impl.loop, &socket->impl.ev);
		}

		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_socket *socket = cio_container_of(stream, struct cio_socket, stream);
	return cio_socket_close(socket);
}

enum cio_error cio_linux_socket_init(struct cio_socket *socket, int client_fd,
                                     struct cio_eventloop *loop,
                                     uint64_t close_timeout_ns,
                                     cio_socket_close_hook_t close_hook)
{
	socket->impl.ev.fd = client_fd;
	socket->impl.ev.write_callback = NULL;
	socket->impl.ev.read_callback = NULL;
	socket->impl.ev.context = socket;
	socket->impl.close_timeout_ns = close_timeout_ns;

	socket->impl.peer_closed_connection = false;

	socket->stream.read_some = stream_read;
	socket->stream.write_some = stream_write;
	socket->stream.close = stream_close;

	socket->impl.loop = loop;
	socket->close_hook = close_hook;

	enum cio_error err = cio_timer_init(&socket->impl.close_timer, socket->impl.loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	err = cio_linux_eventloop_add(socket->impl.loop, &socket->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto close_timer;
	}

	return CIO_SUCCESS;

close_timer:
	cio_timer_close(&socket->impl.close_timer);
	return err;
}

enum cio_error cio_socket_init(struct cio_socket *socket,
                               enum cio_address_family address_family,
                               struct cio_eventloop *loop,
                               uint64_t close_timeout_ns,
                               cio_socket_close_hook_t close_hook)
{
	if (cio_unlikely(socket == NULL) || (loop == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	int socket_fd = cio_linux_socket_create(address_family);
	if (cio_unlikely(socket_fd == -1)) {
		return (enum cio_error)(-errno);
	}

	enum cio_error err = cio_linux_socket_init(socket, socket_fd, loop, close_timeout_ns, close_hook);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		close(socket_fd);
	}

	return err;
}

static void close_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;

	if (err == CIO_SUCCESS) {
		struct cio_socket *socket = (struct cio_socket *)handler_context;
		reset_connection(socket);
	}
}

static void shutdown_socket(struct cio_socket *socket, uint64_t close_timeout_ns)
{
	int ret = shutdown(socket->impl.ev.fd, SHUT_WR);
	if (cio_unlikely(ret == -1)) {
		goto reset_connection;
	}

	socket->impl.ev.context = socket;
	socket->impl.ev.read_callback = read_until_close_callback;

	enum cio_error err = cio_timer_expires_from_now(&socket->impl.close_timer, close_timeout_ns, close_timeout_handler, socket);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto reset_connection;
	}

	err = cio_linux_eventloop_register_read(socket->impl.loop, &socket->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto reset_connection;
	}

	return;

reset_connection:
	reset_connection(socket);
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
	enum cio_error err = CIO_SUCCESS;

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		err = cio_linux_get_socket_error(socket->impl.ev.fd);
	}
	socket->handler(socket, socket->handler_context, err);
}

enum cio_error cio_socket_connect(struct cio_socket *socket, const struct cio_socket_address *endpoint, cio_connect_handler_t handler, void *handler_context)
{
	if (cio_unlikely(socket == NULL) || (endpoint == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely((enum cio_address_family)endpoint->impl.sa.socket_address.addr.sa_family == CIO_ADDRESS_FAMILY_UNSPEC)) {
		return CIO_INVALID_ARGUMENT;
	}

	int ret = connect(socket->impl.ev.fd, &endpoint->impl.sa.socket_address.addr, endpoint->impl.len);
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

enum cio_address_family cio_socket_get_address_family(const struct cio_socket *socket)
{
	struct sockaddr_storage sock_addr;
	memset(&sock_addr, 0, sizeof(sock_addr));
	socklen_t sock_addr_size = sizeof(sock_addr);

	if (getsockname(socket->impl.ev.fd, (struct sockaddr *)&sock_addr, &sock_addr_size) < 0) {
		return CIO_ADDRESS_FAMILY_UNSPEC;
	}

	return (enum cio_address_family)sock_addr.ss_family;
}

enum cio_error cio_socket_set_tcp_no_delay(struct cio_socket *socket, bool on)
{
	int tcp_no_delay = (int)on;

	if (setsockopt(socket->impl.ev.fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
	               sizeof(tcp_no_delay)) < 0) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_socket_set_keep_alive(const struct cio_socket *socket, bool on, unsigned int keep_idle_s,
                                         unsigned int keep_intvl_s, unsigned int keep_cnt)
{
	int keep_alive = 0;

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
	}

	if (setsockopt(socket->impl.ev.fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) == -1) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_socket_set_tcp_fast_open(const struct cio_socket *socket, bool on)
{
	int opt = (int)on;

	int ret = setsockopt(socket->impl.ev.fd, SOL_TCP, TCP_FASTOPEN_CONNECT, &opt, sizeof(opt));
	if (cio_unlikely(ret != 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}
