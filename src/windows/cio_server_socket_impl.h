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

#ifndef CIO_WINDOWS_SERVER_SOCKET_IMPL_H
#define CIO_WINDOWS_SERVER_SOCKET_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include "cio_eventloop.h"
#include "cio_eventloop_impl.h"

struct cio_windows_listen_socket {
	SOCKET accept_socket;
	int address_family;
	LPFN_ACCEPTEX accept_ex;
	bool bound;
	HANDLE fd;
	HANDLE network_event;
	struct cio_event_notifier en;
	char accept_buffer[sizeof(struct sockaddr_storage) * 2];
};

struct cio_server_socket_impl {
	struct cio_eventloop *loop;
	struct cio_windows_listen_socket listen_socket;
};

#ifdef __cplusplus
}
#endif

#endif // CIO_WINDOWS_SERVER_SOCKET_IMPL_H
