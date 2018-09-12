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

static enum cio_error socket_set_reuse_address(struct cio_server_socket *ss, bool on)
{
	char reuse;
	if (on) {
		reuse = 1;
	} else {
		reuse = 0;
	}

	if (cio_unlikely(setsockopt(ss->impl.listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse,
	                            sizeof(reuse)) < 0)) {
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
		struct sockaddr_in6 address;
		memset(&address, 0, sizeof(address));
		address.sin6_family = AF_INET6;
		address.sin6_addr = in6addr_any;
		address.sin6_port = htons(port);
		if (cio_unlikely(bind(ss->impl.listen_socket, (struct sockaddr *)&address, sizeof(address)) == -1)) {
			int err = WSAGetLastError();
			return (enum cio_error)(-err);
		} else {
			return CIO_SUCCESS;
		}
	} else {
		ret = getaddrinfo(bind_address, server_port_string, &hints, &servinfo);
		if (cio_unlikely(ret != 0)) {
			return (enum cio_error)(-ret);
		}

		for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
			if (cio_likely(bind(ss->impl.listen_socket, rp->ai_addr, rp->ai_addrlen) == 0)) {
				break;
			}
		}

		FreeAddrInfoW(servinfo);

		if (rp == NULL) {
			return CIO_INVALID_ARGUMENT;
		}

		return CIO_SUCCESS;
	}
}


static void accept_callback(void *context)
{

}

static enum cio_error socket_accept(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	if (cio_unlikely(handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	ss->handler = handler;
	ss->handler_context = handler_context;
	ss->impl.listen_event.callback = accept_callback;
	ss->impl.listen_event.context = ss;

	if (cio_unlikely(listen(ss->impl.listen_socket, ss->backlog) < 0)) {
		int err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	if (CreateIoCompletionPort(ss->impl.listen_socket, ss->impl.loop->loop_completion_port, &ss->impl.listen_event, 1) == NULL) {
		int err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	DWORD dw_bytes;
	GUID guid_accept_ex = WSAID_ACCEPTEX;
	LPFN_ACCEPTEX accept_ex = NULL;
	int status = WSAIoctl(ss->impl.listen_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid_accept_ex, sizeof(guid_accept_ex), &accept_ex, sizeof(accept_ex), &dw_bytes, NULL, NULL);
	if (status != 0) {
		int err = WSAGetLastError();
		return (enum cio_error)(-err);
	}

	ss->impl.accept_socket = WSASocket(AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (cio_unlikely(ss->impl.accept_socket == INVALID_SOCKET)) {
		int err = WSAGetLastError();
		return (enum cio_error)(-err);
	}
	
	DWORD bytes_received;
	BOOL ret = accept_ex(ss->impl.listen_socket, ss->impl.accept_socket, ss->impl.accept_buffer, 0,
	                     sizeof(struct sockaddr_storage), sizeof(struct sockaddr_storage),
	                     &bytes_received, &ss->impl.listen_event.overlapped);

	if (ret == FALSE) {
		int err = WSAGetLastError();
		if (cio_likely(err == WSA_IO_PENDING)) {
			return CIO_SUCCESS;		
		} else {
			return (enum cio_error)(-err);
		}
	}

	return CIO_SUCCESS;
}

static void socket_close(struct cio_server_socket *ss)
{

	// TODO: Caution. When closing a socket and there are still pending overlapped operations,
	// we cannot just free the memory which holds the overlapped structure. We need to remove it from the
	// The eventloop first and only close the socket afterwards.

	closesocket(ss->impl.listen_socket);
	if (ss->close_hook != NULL) {
		ss->close_hook(ss);
	}
}

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      cio_alloc_client alloc_client,
                                      cio_free_client free_client,
                                      cio_server_socket_close_hook close_hook)
{
	int err;
	SOCKET s = WSASocket(AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (cio_unlikely(s == INVALID_SOCKET)) {
		err = WSAGetLastError();
		return (enum cio_error)-err;
	}

	ss->impl.listen_socket = s;
	ss->impl.listen_event.overlapped.hEvent = WSACreateEvent();
	if (cio_unlikely(ss->impl.listen_event.overlapped.hEvent == WSA_INVALID_EVENT)) {
		err = WSAGetLastError();
		goto create_event_failed;
	}

	u_long on = 1;
	int ret = ioctlsocket(s, FIONBIO, &on);
	if (cio_unlikely(ret != 0)) {
		err = WSAGetLastError();
		goto ioctl_failed;
	}

	ss->impl.loop = loop;
	ss->backlog = (int)backlog;
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->close_hook = close_hook;

	ss->set_reuse_address = socket_set_reuse_address;
	ss->bind = socket_bind;
	ss->accept = socket_accept;
	ss->close = socket_close;

	return CIO_SUCCESS;

ioctl_failed:
	WSACloseEvent(ss->impl.listen_event.overlapped.hEvent);
create_event_failed:
	closesocket(s);
	return (enum cio_error)-err;
}
