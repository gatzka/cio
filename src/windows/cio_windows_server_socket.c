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

#include <Windows.h>
#include <stdio.h>

#include "cio_endian.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_server_socket.h"
#include "cio_windows_socket_utils.h"
#include "cio_util.h"
#include "cio_windows_socket.h"

static enum cio_error prepare_accept_socket(struct cio_windows_listen_socket *s);

static struct cio_server_socket *get_server_socket(const struct cio_windows_listen_socket *wls)
{
	struct cio_server_socket_imp *impl = cio_container_of(wls, struct cio_server_socket_impl, listen_socket);
	struct cio_server_socket *ss = cio_container_of(impl, struct cio_server_socket, impl);
	return ss;
}

static void try_free(struct cio_windows_listen_socket *wls)
{
	if (wls->en.overlapped_operations_in_use == 0) {
		closesocket((SOCKET)wls->fd);
		struct cio_server_socket *ss = get_server_socket(wls);
		if (ss->impl.listen_socket.bound == false) {
			if (ss->close_hook != NULL) {
				ss->close_hook(ss);
			}
		}
	}
}

static void accept_callback(struct cio_event_notifier *ev)
{
	struct cio_windows_listen_socket *wls = cio_container_of(ev, struct cio_windows_listen_socket, en);
	struct cio_server_socket *ss = get_server_socket(wls);

	struct cio_socket *s;
	enum cio_error error_code = CIO_SUCCESS;
	DWORD bytes_transferred;
	DWORD flags = 0;
	BOOL rc = WSAGetOverlappedResult((SOCKET)wls->fd, &ev->overlapped, &bytes_transferred, FALSE, &flags);
	wls->en.overlapped_operations_in_use--;
	if (cio_unlikely(rc == FALSE)) {
		int error = WSAGetLastError();
		if (error == WSA_OPERATION_ABORTED) {
			wls->bound = false;
			try_free(wls);
			return;
		}

		error_code = (enum cio_error)(-error);
		s = NULL;
	} else {
		SOCKET client_fd = wls->accept_socket;
		int ret = setsockopt(client_fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&wls->fd, sizeof(wls->fd));
		if (cio_unlikely(ret == SOCKET_ERROR)) {
			error_code = (enum cio_error)(-WSAGetLastError());
			s = NULL;
			goto setsockopt_failed;
		}

		error_code = prepare_accept_socket(wls);
		if (cio_unlikely(error_code != CIO_SUCCESS)) {
			s = NULL;
			goto prepare_failed;
		}

		s = ss->alloc_client();
		if (cio_unlikely(s == NULL)) {
			error_code = CIO_NO_MEMORY;
			goto alloc_failed;
		}

		error_code = cio_windows_add_handle_to_completion_port((HANDLE)client_fd, ss->impl.loop, &s->stream);
		if (cio_unlikely(error_code != CIO_SUCCESS)) {
			goto socket_init_failed;
		}

		error_code = cio_windows_socket_init(s, client_fd, ss->impl.loop, 0, ss->free_client);
		if (cio_unlikely(error_code != CIO_SUCCESS)) {
			goto socket_init_failed;
		}

		goto ok;
	socket_init_failed:
		ss->free_client(s);
		s = NULL;
	alloc_failed:
		closesocket(client_fd);
	}

setsockopt_failed:
prepare_failed:
ok:
	ss->handler(ss, ss->handler_context, error_code, s);
}

static enum cio_error prepare_accept_socket(struct cio_windows_listen_socket *wls)
{
	enum cio_error err;
	wls->accept_socket = WSASocketW(wls->address_family, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (cio_unlikely(wls->accept_socket == INVALID_SOCKET)) {
		return (enum cio_error)(-WSAGetLastError());
	}

	memset(&wls->en.overlapped, 0, sizeof(wls->en.overlapped));

	DWORD bytes_received;
	BOOL ret = wls->accept_ex((SOCKET)wls->fd, wls->accept_socket, wls->accept_buffer, 0,
	                          sizeof(struct sockaddr_storage), sizeof(struct sockaddr_storage),
	                          &bytes_received, &wls->en.overlapped);
	if (ret == FALSE) {
		int rc = WSAGetLastError();
		if (cio_likely(rc != WSA_IO_PENDING)) {
			err = (enum cio_error)(-rc);
			goto accept_ex_failed;
		}
	}

	wls->en.overlapped_operations_in_use++;

	return CIO_SUCCESS;

accept_ex_failed:
	closesocket(wls->accept_socket);
	return err;
}

static void close_listen_socket(struct cio_windows_listen_socket *wls)
{
	CancelIo(wls->fd);
	if (wls->accept_socket != INVALID_SOCKET) {
		closesocket(wls->accept_socket);
	}

	try_free(wls);
}

static enum cio_error create_listen_socket(struct cio_windows_listen_socket *socket, enum cio_address_family address_family, struct cio_eventloop *loop)
{
	int err;
	if (address_family == CIO_INET4_ADDRESS) {
		socket->address_family = AF_INET;
	} else {
		socket->address_family = AF_INET6;
	}

	socket->bound = false;
	socket->accept_socket = INVALID_SOCKET;
	socket->fd = (HANDLE)cio_windows_socket_create(socket->address_family, loop, socket);
		
	if (cio_unlikely((SOCKET)socket->fd == INVALID_SOCKET)) {
		err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	DWORD dw_bytes;
	GUID guid_accept_ex = WSAID_ACCEPTEX;
	int status = WSAIoctl((SOCKET)socket->fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid_accept_ex, sizeof(guid_accept_ex), &socket->accept_ex, sizeof(socket->accept_ex), &dw_bytes, NULL, NULL);
	if (status != 0) {
		err = WSAGetLastError();
		goto WSAioctl_failed;
	}

	socket->en.callback = accept_callback;
	socket->en.overlapped_operations_in_use = 0;

	return CIO_SUCCESS;

WSAioctl_failed:
	close_listen_socket(socket);
	return (enum cio_error)(-err);
}

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                                 struct cio_eventloop *loop,
                                                 unsigned int backlog,
                                                 enum cio_socket_address_family family,
                                                 cio_alloc_client alloc_client,
                                                 cio_free_client free_client,
                                                 uint64_t close_timeout_ns,
                                                 cio_server_socket_close_hook close_hook)
{
	(void)close_timeout_ns;

	ss->backlog = (int)backlog;
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->close_hook = close_hook;
	ss->impl.loop = loop;

	ss->impl.listen_socket.accept_socket = INVALID_SOCKET;
	
	enum cio_error err = create_listen_socket(&ss->impl.listen_socket, family, loop);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_accept(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	if (cio_unlikely(handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	ss->handler = handler;
	ss->handler_context = handler_context;

	if (ss->impl.listen_socket.bound) {
		if (cio_unlikely(listen((SOCKET)ss->impl.listen_socket.fd, ss->backlog) < 0)) {
			int err = WSAGetLastError();
			return (enum cio_error)(-err);
		}

		enum cio_error error = prepare_accept_socket(&ss->impl.listen_socket);
		if (cio_unlikely(error != CIO_SUCCESS)) {
			return error;
		}
	}

	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_set_reuse_address(struct cio_server_socket *ss, bool on)
{
	char reuse;
	if (on) {
		reuse = 1;
	} else {
		reuse = 0;
	}

	if (cio_unlikely(setsockopt((SOCKET)ss->impl.listen_socket.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)) {
		int err = GetLastError();
		return (enum cio_error)(-err);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_bind(struct cio_server_socket *ss, const struct cio_inet_socket_address *endpoint)
{
	if (cio_unlikely((ss == NULL) || (endpoint == NULL))) {
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

	int ret = bind((SOCKET)ss->impl.listen_socket.fd, addr, addr_len);
	if (cio_unlikely(ret != 0)) {
		int err = GetLastError();
		return (enum cio_error)(-err);
	}

	return CIO_SUCCESS;
}

void cio_server_socket_close(struct cio_server_socket *ss)
{
	close_listen_socket(&ss->impl.listen_socket);
}
