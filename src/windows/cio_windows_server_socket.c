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

#include <Winsock2.h>

#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_server_socket.h"

// TODO: Caution. When closing a socket and there are still pending overlapped operations,
// we cannot just free the memory which holds the overlapped structure. We need to remove it from the
// The eventloop first and only close the socket afterwards.

static enum cio_error socket_set_reuse_address(struct cio_server_socket *ss, bool on)
{
	char reuse;
	if (on) {
		reuse = 1;
	} else {
		reuse = 0;
	}

	if (cio_unlikely(setsockopt(ss->ev.s, SOL_SOCKET, SO_REUSEADDR, &reuse,
	                            sizeof(reuse)) < 0)) {
		int err = GetLastError();
		return (enum cio_error)(-err);
	}

	return CIO_SUCCESS;
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

	ss->ev.s = s;
	ss->ev.overlapped.hEvent = WSACreateEvent();
	if (cio_unlikely(ss->ev.overlapped.hEvent == WSA_INVALID_EVENT)) {
		err = WSAGetLastError();
		goto create_event_failed;
	}

	u_long on = 1;
	int ret = ioctlsocket(s, FIONBIO, &on);
	if (cio_unlikely(ret != 0)) {
		err = WSAGetLastError();
		goto ioctl_failed;
	}

	ss->loop = loop;
	ss->backlog = (int)backlog;
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->close_hook = close_hook;

	ss->set_reuse_address = socket_set_reuse_address;

	return CIO_SUCCESS;

ioctl_failed:
	WSACloseEvent(ss->ev.overlapped.hEvent);
create_event_failed:
	closesocket(s);
	return (enum cio_error)-err;
}
