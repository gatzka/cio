/*
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

#include <Windows.h>
#include <stdio.h>

#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_server_socket.h"
#include "cio_util.h"
#include "cio_windows_socket.h"

static SOCKET create_win_socket_nonblocking(int address_family)
{
	SOCKET s = WSASocket(address_family, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (cio_unlikely(s == INVALID_SOCKET)) {
		return INVALID_SOCKET;
	}

	u_long on = 1;
	int ret = ioctlsocket(s, FIONBIO, &on);
	if (cio_unlikely(ret != 0)) {
		closesocket(s);
		return INVALID_SOCKET;
	}

	return s;
}

static enum cio_error prepare_accept_socket(struct cio_windows_listen_socket *s)
{
	s->accept_socket = WSASocket(s->address_family, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (cio_unlikely(s->accept_socket == INVALID_SOCKET)) {
		int err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	DWORD bytes_received;
	BOOL ret = s->accept_ex((SOCKET)s->listen_event.fd, s->accept_socket, s->accept_buffer, 0,
	                                                 sizeof(struct sockaddr_storage), sizeof(struct sockaddr_storage),
	                                                 &bytes_received, &s->listen_event.overlapped);
	if (ret == FALSE) {
		int err = WSAGetLastError();
		if (cio_likely(err != WSA_IO_PENDING)) {
			return (enum cio_error)(-err);
		}
	}

	return CIO_SUCCESS;
}

static enum cio_error socket_set_reuse_address(struct cio_server_socket *ss, bool on)
{
	char reuse;
	if (on) {
		reuse = 1;
	} else {
		reuse = 0;
	}

	if (cio_unlikely(setsockopt((SOCKET)ss->impl.listen_socket_ipv4.listen_event.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)) {
		int err = GetLastError();
		return (enum cio_error)(-err);
	}

	if (cio_unlikely(setsockopt((SOCKET)ss->impl.listen_socket_ipv6.listen_event.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)) {
		int err = GetLastError();
		return (enum cio_error)(-err);
	}

	return CIO_SUCCESS;
}

static enum cio_error socket_bind(struct cio_server_socket *ss, const char *bind_address, uint16_t port)
{
	struct addrinfo hints;
	char server_port_string[6];
	struct addrinfo *servinfo = NULL;
	int ret;
	struct addrinfo *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE | AI_V4MAPPED | AI_NUMERICHOST;

	snprintf(server_port_string, sizeof(server_port_string), "%d", port);

	if (bind_address == NULL) {
		struct sockaddr_in6 address_v6;
		memset(&address_v6, 0, sizeof(address_v6));
		address_v6.sin6_family = AF_INET6;
		address_v6.sin6_addr = in6addr_any;
		address_v6.sin6_port = htons(port);
		if (cio_unlikely(bind((SOCKET)ss->impl.listen_socket_ipv6.listen_event.fd, (struct sockaddr *)&address_v6, sizeof(address_v6)) == -1)) {
			int err = WSAGetLastError();
			return (enum cio_error)(-err);
		}
		ss->impl.listen_socket_ipv6.bound = true;

		struct sockaddr_in address_v4;
		memset(&address_v4, 0, sizeof(address_v4));
		address_v4.sin_family = AF_INET;
		address_v4.sin_addr = in4addr_any;
		address_v4.sin_port = htons(port);
		if (cio_unlikely(bind((SOCKET)ss->impl.listen_socket_ipv4.listen_event.fd, (struct sockaddr *)&address_v4, sizeof(address_v4)) == -1)) {
			int err = WSAGetLastError();
			return (enum cio_error)(-err);
		}
		ss->impl.listen_socket_ipv4.bound = true;

		return CIO_SUCCESS;
	} else {
		ret = getaddrinfo(bind_address, server_port_string, &hints, &servinfo);
		if (cio_unlikely(ret != 0)) {
			return (enum cio_error)(-ret);
		}

		for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
			switch (rp->ai_family) {
			case AF_INET:
				if (cio_likely(bind((SOCKET)ss->impl.listen_socket_ipv4.listen_event.fd, rp->ai_addr, (int)rp->ai_addrlen) == 0)) {
					ss->impl.listen_socket_ipv4.bound = true;
					goto out;
				}
				break;
			case AF_INET6:
				if (cio_likely(bind((SOCKET)ss->impl.listen_socket_ipv6.listen_event.fd, rp->ai_addr, (int)rp->ai_addrlen) == 0)) {
					ss->impl.listen_socket_ipv6.bound = true;
					goto out;
				}
				break;
			}
		}
	out:
		freeaddrinfo(servinfo);

		if (rp == NULL) {
			return CIO_INVALID_ARGUMENT;
		}

		return CIO_SUCCESS;
	}
}

static void accept_callback(void *context)
{
	struct cio_event_notifier *ev = (struct cio_event_notifier *)context;

	struct cio_windows_listen_socket *wls = container_of(ev, struct cio_windows_listen_socket, listen_event);
	struct cio_server_socket_imp *impl;
	if (wls->address_family == AF_INET) {
		impl = container_of(wls, struct cio_server_socket_impl, listen_socket_ipv4);
	} else {
		impl = container_of(wls, struct cio_server_socket_impl, listen_socket_ipv6);
	}

	struct cio_server_socket *ss = container_of(impl, struct cio_server_socket, impl);

	if (cio_unlikely(ev->last_error == ERROR_OPERATION_ABORTED)) {
		// This seems to be the condition when a server socket is closed.
		ev->last_error = ERROR_SUCCESS;
		if (ss->close_hook != NULL) {
			ss->close_hook(ss);
		}

		return;
	}
	
	enum cio_error err;
	SOCKET client_fd = wls->accept_socket;
	int ret = setsockopt(client_fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&wls->listen_event.fd, sizeof(wls->listen_event.fd));
	if (cio_unlikely(ret == SOCKET_ERROR)) {
		err = (enum cio_error)(-WSAGetLastError());
		goto setsockopt_failed;
	}

	err = prepare_accept_socket(wls);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto prepare_failed;
	}

	struct cio_socket *s = ss->alloc_client();
	if (cio_unlikely(s == NULL)) {
		err = CIO_NO_MEMORY;
		goto alloc_failed;
	}

	err = cio_windows_socket_init(s, client_fd, ss->impl.loop, ss->free_client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto socket_init_failed;
	}

	ss->handler(ss, ss->handler_context, CIO_SUCCESS, s);
	return;

socket_init_failed:
	ss->free_client(s);
alloc_failed:
	closesocket(client_fd);
setsockopt_failed:
prepare_failed:
	ss->handler(ss, ss->handler_context, err, NULL);
}

static enum cio_error socket_accept(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	if (cio_unlikely(handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	ss->handler = handler;
	ss->handler_context = handler_context;
	ss->impl.listen_socket_ipv4.listen_event.callback = accept_callback;
	ss->impl.listen_socket_ipv4.listen_event.context = &ss->impl.listen_socket_ipv4;
	ss->impl.listen_socket_ipv6.listen_event.callback = accept_callback;
	ss->impl.listen_socket_ipv6.listen_event.context = &ss->impl.listen_socket_ipv6;

	if (ss->impl.listen_socket_ipv4.bound) {
		if (cio_unlikely(listen((SOCKET)ss->impl.listen_socket_ipv4.listen_event.fd, ss->backlog) < 0)) {
			int err = WSAGetLastError();
			return (enum cio_error)(-err);
		}

		enum cio_error error = prepare_accept_socket(&ss->impl.listen_socket_ipv4);
		if (cio_unlikely(error != CIO_SUCCESS)) {
			return error;
		}
	}

	if (ss->impl.listen_socket_ipv6.bound) {
		if (cio_unlikely(listen((SOCKET)ss->impl.listen_socket_ipv6.listen_event.fd, ss->backlog) < 0)) {
			int err = WSAGetLastError();
			return (enum cio_error)(-err);
		}

		enum cio_error error = prepare_accept_socket(&ss->impl.listen_socket_ipv6);
		if (cio_unlikely(error != CIO_SUCCESS)) {
			return error;
		}
	}

	return CIO_SUCCESS;
}

static void close_listen_socket(struct cio_windows_listen_socket *socket, const struct cio_eventloop *loop)
{
	cio_windows_eventloop_remove(&socket->listen_event, loop);
	closesocket((SOCKET)socket->listen_event.fd);
	if (socket->accept_socket != INVALID_SOCKET) {
		closesocket(socket->accept_socket);
	}
}

static void socket_close(struct cio_server_socket *ss)
{
	close_listen_socket(&ss->impl.listen_socket_ipv4, ss->impl.loop);
	close_listen_socket(&ss->impl.listen_socket_ipv6, ss->impl.loop);
}

static enum cio_error create_listen_socket(struct cio_windows_listen_socket *socket, int address_family, struct cio_eventloop *loop)
{
	int err;
	socket->address_family = address_family;
	socket->bound = false;
	socket->listen_event.fd = (HANDLE)create_win_socket_nonblocking(address_family);

	if (cio_unlikely((SOCKET)socket->listen_event.fd == INVALID_SOCKET)) {
		err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	enum cio_error error = cio_windows_eventloop_add(&socket->listen_event, loop);
	if (cio_unlikely(error != CIO_SUCCESS)) {
		err = error;
		goto eventloop_add_failed;
	}

	if (cio_unlikely(WSAEventSelect((SOCKET)socket->listen_event.fd, socket->listen_event.overlapped.hEvent, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR)) {
		err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	DWORD dw_bytes;
	GUID guid_accept_ex = WSAID_ACCEPTEX;
	int status = WSAIoctl((SOCKET)socket->listen_event.fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid_accept_ex, sizeof(guid_accept_ex), &socket->accept_ex, sizeof(socket->accept_ex), &dw_bytes, NULL, NULL);
	if (status != 0) {
		err = -WSAGetLastError();
		goto WSAioctl_failed;
	}

	return CIO_SUCCESS;

WSAioctl_failed:
	cio_windows_eventloop_remove(&socket->listen_event, loop);
eventloop_add_failed:
	closesocket((SOCKET)socket->listen_event.fd);
	return (enum cio_error)err;
}

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      cio_alloc_client alloc_client,
                                      cio_free_client free_client,
                                      cio_server_socket_close_hook close_hook)
{
	ss->impl.loop = loop;
	ss->backlog = (int)backlog;
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->close_hook = close_hook;

	ss->set_reuse_address = socket_set_reuse_address;
	ss->bind = socket_bind;
	ss->accept = socket_accept;
	ss->close = socket_close;

	ss->impl.listen_socket_ipv4.accept_socket = INVALID_SOCKET;
	ss->impl.listen_socket_ipv6.accept_socket = INVALID_SOCKET;

	enum cio_error err = create_listen_socket(&ss->impl.listen_socket_ipv4, AF_INET, ss->impl.loop);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	err = create_listen_socket(&ss->impl.listen_socket_ipv6, AF_INET6, ss->impl.loop);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto create_listen_socket_v6_failed;
	}

	return CIO_SUCCESS;

create_listen_socket_v6_failed:
	close_listen_socket(&ss->impl.listen_socket_ipv4, loop);
	return err;
}
