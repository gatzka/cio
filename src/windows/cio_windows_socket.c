/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2018> <Stephan Gatzka>
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

#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#include <Windows.h>
#include <Ws2tcpip.h>
#include <malloc.h>
#include <mswsock.h>
#include <Mstcpip.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_socket.h"
#include "cio_socket_address.h"
#include "cio_util.h"
#include "cio_windows_socket.h"
#include "cio_windows_socket_utils.h"

static void try_free(struct cio_socket *s)
{
	if ((s->impl.read_event.overlapped_operations_in_use == 0) && (s->impl.write_event.overlapped_operations_in_use == 0)) {
		closesocket((SOCKET)s->impl.fd);
		if (s->close_hook != NULL) {
			s->close_hook(s);
		}
	}
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
	return cio_socket_close(s);
}

static void read_callback(struct cio_event_notifier *ev)
{
	struct cio_socket_impl *impl = cio_container_of(ev, struct cio_socket_impl, read_event);
	struct cio_socket *s = cio_container_of(impl, struct cio_socket, impl);

	DWORD recv_bytes;
	DWORD flags = 0;
	BOOL rc = WSAGetOverlappedResult((SOCKET)impl->fd, &ev->overlapped, &recv_bytes, FALSE, &flags);
	ev->overlapped_operations_in_use--;
	enum cio_error error_code;
	if (cio_unlikely(rc == FALSE)) {
		int error = WSAGetLastError();
		if (error == WSA_OPERATION_ABORTED) {
			try_free(s);
			return;
		}

		error_code = (enum cio_error)(-error);
	} else {
		if (recv_bytes == 0) {
			error_code = CIO_EOF;
		} else {
			s->stream.read_buffer->add_ptr += (size_t)recv_bytes;
			error_code = CIO_SUCCESS;
		}
	}

	s->stream.read_handler(&s->stream, s->stream.read_handler_context, error_code, s->stream.read_buffer);
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);

	s->stream.read_buffer = buffer;
	s->stream.read_handler = handler;
	s->stream.read_handler_context = handler_context;

	WSABUF wsa_buffer;
	wsa_buffer.len = (ULONG)cio_read_buffer_space_available(buffer);
	wsa_buffer.buf = (CHAR *)buffer->add_ptr;
	DWORD flags = 0;

	memset(&s->impl.read_event.overlapped, 0, sizeof(s->impl.read_event.overlapped));

	int rc = WSARecv((SOCKET)s->impl.fd, &wsa_buffer, 1, NULL, &flags, &s->impl.read_event.overlapped, NULL);
	if (rc == SOCKET_ERROR) {
		int error = WSAGetLastError();
		if (cio_unlikely(error != WSA_IO_PENDING)) {
			return (enum cio_error)(-error);
		}
	}

	s->impl.read_event.overlapped_operations_in_use++;
	return CIO_SUCCESS;
}

static void write_callback(struct cio_event_notifier *ev)
{
	struct cio_socket_impl *impl = cio_container_of(ev, struct cio_socket_impl, write_event);
	struct cio_socket *s = cio_container_of(impl, struct cio_socket, impl);

	DWORD bytes_sent;
	DWORD flags = 0;
	BOOL rc = WSAGetOverlappedResult((SOCKET)impl->fd, &ev->overlapped, &bytes_sent, FALSE, &flags);
	ev->overlapped_operations_in_use--;
	enum cio_error error_code = CIO_SUCCESS;
	if (cio_unlikely(rc == FALSE)) {
		int error = WSAGetLastError();
		if (error == WSA_OPERATION_ABORTED) {
			try_free(s);
			return;
		}

		bytes_sent = 0;
		error_code = (enum cio_error)(-error);
	}

	s->stream.write_handler(&s->stream, s->stream.write_handler_context, s->stream.write_buffer, error_code, (size_t)bytes_sent);
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
	s->stream.write_handler = handler;
	s->stream.write_handler_context = handler_context;
	s->stream.write_buffer = buffer;

	size_t chain_length = cio_write_buffer_get_num_buffer_elements(buffer);

	WSABUF *wsa_buffers = _malloca(sizeof(*wsa_buffers) * chain_length);
	if (cio_unlikely(wsa_buffers == NULL)) {
		return CIO_NO_BUFFER_SPACE;
	}

	struct cio_write_buffer *wb = buffer->next;
	for (size_t i = 0; i < chain_length; i++) {
		wsa_buffers[i].buf = (void *)wb->data.element.const_data;
		wsa_buffers[i].len = (ULONG)wb->data.element.length;
		wb = wb->next;
	}

	memset(&s->impl.write_event.overlapped, 0, sizeof(s->impl.write_event.overlapped));

	int rc = WSASend((SOCKET)s->impl.fd, wsa_buffers, (DWORD)chain_length, NULL, 0, &s->impl.write_event.overlapped, NULL);
	if (rc == SOCKET_ERROR) {
		int error = WSAGetLastError();
		if (cio_unlikely(error != WSA_IO_PENDING)) {
			return (enum cio_error)(-error);
		}
	}

	s->impl.write_event.overlapped_operations_in_use++;
	return CIO_SUCCESS;
}

enum cio_error cio_windows_socket_init(struct cio_socket *s, SOCKET client_fd,
                                       struct cio_eventloop *loop,
                                       uint64_t close_timeout_ns,
                                       cio_socket_close_hook close_hook)
{
	if (cio_unlikely((s == NULL) || (loop == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	s->impl.fd = (HANDLE)client_fd;

	s->impl.read_event.callback = read_callback;
	s->impl.read_event.overlapped_operations_in_use = 0;
	s->impl.write_event.callback = write_callback;
	s->impl.write_event.overlapped_operations_in_use = 0;
	s->impl.loop = loop;
	s->impl.close_timeout_ns = close_timeout_ns;
	s->close_hook = close_hook;

	s->stream.read_some = stream_read;
	s->stream.write_some = stream_write;
	s->stream.close = stream_close;

	return CIO_SUCCESS;
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
	
	if (cio_unlikely((address_family != CIO_ADDRESS_FAMILY_INET4) && (address_family != CIO_ADDRESS_FAMILY_INET6))) {
		return CIO_INVALID_ARGUMENT;
	}

	int domain = (int)address_family;

	SOCKET socket_fd = cio_windows_socket_create(domain, loop, &socket->impl);
	if (cio_unlikely(socket_fd == -1)) {
		return (enum cio_error)(-errno);
	}

	return cio_windows_socket_init(socket, socket_fd, loop, close_timeout_ns, close_hook);
}

enum cio_error cio_socket_close(struct cio_socket *s)
{
	if (cio_unlikely(s == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	CancelIo(s->impl.fd);
	shutdown((SOCKET)s->impl.fd, SD_BOTH);
	try_free(s);

	return CIO_SUCCESS;
}

static void connect_callback(struct cio_event_notifier *ev)
{
	struct cio_socket_impl *impl = cio_container_of(ev, struct cio_socket_impl, write_event);
	struct cio_socket *socket = cio_container_of(impl, struct cio_socket, impl);

	DWORD bytes_transferred;
	DWORD flags = 0;
	BOOL rc = WSAGetOverlappedResult((SOCKET)impl->fd, &ev->overlapped, &bytes_transferred, FALSE, &flags);
	impl->write_event.overlapped_operations_in_use--;

	if (cio_unlikely(rc == FALSE)) {
		int err = WSAGetLastError();
		socket->handler(socket, socket->handler_context, (enum cio_error)(-err));
		return;
	}

	rc = setsockopt((SOCKET)impl->fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
	if (cio_unlikely(rc == SOCKET_ERROR)) {
		int err = WSAGetLastError();
		socket->handler(socket, socket->handler_context, (enum cio_error)(-err));
		return;
	}

	socket->impl.write_event.callback = write_callback;
	socket->handler(socket, socket->handler_context, CIO_SUCCESS);
}

enum cio_error cio_socket_connect(struct cio_socket *socket, const struct cio_socket_address *endpoint, cio_connect_handler handler, void *handler_context)
{
	if (cio_unlikely(socket == NULL) || (endpoint == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely((enum cio_address_family)endpoint->impl.socket_address.addr.sa_family == CIO_ADDRESS_FAMILY_UNSPEC)) {
		return CIO_INVALID_ARGUMENT;
	}

	const struct sockaddr *addr;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	int addr_len;
	if (endpoint->impl.socket_address.addr.sa_family == CIO_ADDRESS_FAMILY_INET4) {
		memset(&addr4, 0, sizeof(addr4));
		addr4.sin_family = AF_INET;
		addr4.sin_addr.s_addr = INADDR_ANY;
		addr4.sin_port = 0;
		addr = (struct sockaddr *)&addr4;
		addr_len = sizeof(addr4);
	} else {
		memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		addr6.sin6_addr = in6addr_any;
		addr6.sin6_port = 0;
		addr = (struct sockaddr *)&addr6;
		addr_len = sizeof(addr6);
	}

	int rc = bind((SOCKET)socket->impl.fd, addr, addr_len);
	if (rc != 0) {
		int err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	if ((enum cio_address_family)endpoint->impl.socket_address.addr.sa_family == CIO_ADDRESS_FAMILY_INET4) {
		addr = (const struct sockaddr *)&endpoint->impl.inet_addr4.impl.in;
		addr_len = sizeof(endpoint->impl.inet_addr4.impl.in);
	} else {
		addr = (const struct sockaddr *)&endpoint->impl.inet_addr6.impl.in6;
		addr_len = sizeof(endpoint->impl.inet_addr6.impl.in6);
	}

	if (endpoint->impl.socket_address.addr.sa_family == CIO_ADDRESS_FAMILY_INET4) {
		addr = (const struct sockaddr *)&endpoint->impl.inet_addr4.impl.in;
		addr_len = sizeof(endpoint->impl.inet_addr4.impl.in);
	} else {
		addr = (const struct sockaddr *)&endpoint->impl.inet_addr6.impl.in6;
		addr_len = sizeof(endpoint->impl.inet_addr6.impl.in6);
	}

	DWORD dw_bytes;
	GUID guid_connect_ex = WSAID_CONNECTEX;
	int status = WSAIoctl((SOCKET)socket->impl.fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid_connect_ex, sizeof(guid_connect_ex), &socket->impl.connect_ex, sizeof(socket->impl.connect_ex), &dw_bytes, NULL, NULL);
	if (status != 0) {
		int err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	DWORD bytes_sent = 0;
	memset(&socket->impl.write_event.overlapped, 0, sizeof(socket->impl.write_event.overlapped));
	BOOL ret = socket->impl.connect_ex((SOCKET)socket->impl.fd, addr, addr_len, NULL, 0, &bytes_sent, &socket->impl.write_event.overlapped);
	if (ret == TRUE) {
		handler(socket, handler_context, CIO_SUCCESS);
	} else {
		rc = WSAGetLastError();
		if (cio_likely(rc != WSA_IO_PENDING)) {
			return (enum cio_error)(-rc);
		}

		socket->handler = handler;
		socket->handler_context = handler_context;
		socket->impl.write_event.callback = connect_callback;
	}

	socket->impl.write_event.overlapped_operations_in_use++;
	return CIO_SUCCESS;
}

enum cio_error cio_socket_set_tcp_no_delay(struct cio_socket *s, bool on)
{
	char tcp_no_delay = (char)on;

	if (cio_unlikely(setsockopt((SOCKET)s->impl.fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
	                            sizeof(tcp_no_delay)) == SOCKET_ERROR)) {
		return (enum cio_error)(-WSAGetLastError());
	}

	return CIO_SUCCESS;
}

enum cio_error cio_socket_set_keep_alive(struct cio_socket *s, bool on, unsigned int keep_idle_s,
                                         unsigned int keep_intvl_s, unsigned int keep_cnt)
{
	(void)keep_cnt;

	struct tcp_keepalive alive = {.onoff = on, .keepalivetime = keep_idle_s, .keepaliveinterval = keep_intvl_s};

	DWORD bytes_returned;
	if (cio_unlikely(WSAIoctl((SOCKET)s->impl.fd, SIO_KEEPALIVE_VALS, &alive, sizeof(alive), NULL, 0, &bytes_returned, NULL, NULL) == SOCKET_ERROR)) {
		return (enum cio_error)(-WSAGetLastError());
	}

	return CIO_SUCCESS;
}

struct cio_io_stream *cio_socket_get_io_stream(struct cio_socket *s)
{
	return &s->stream;
}

enum cio_error cio_socket_set_tcp_fast_open(struct cio_socket *socket, bool on)
{
	(void)socket;
	(void)on;

	return CIO_SUCCESS;
}
