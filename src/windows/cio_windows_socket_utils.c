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

#include <WinSock2.h>

#include "cio_compiler.h"
#include "cio_eventloop_impl.h"
#include "cio_windows_socket_utils.h"

SOCKET cio_windows_socket_create(int address_family, const struct cio_eventloop *loop, void *socket_impl)
{
	SOCKET s = WSASocketW(address_family, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (cio_unlikely(s == INVALID_SOCKET)) {
		return INVALID_SOCKET;
	}

	enum cio_error err = cio_windows_add_handle_to_completion_port((HANDLE)s, loop, socket_impl);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		closesocket(s);
		return INVALID_SOCKET;
	}

	return s;
}