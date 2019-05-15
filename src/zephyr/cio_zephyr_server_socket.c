/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2019> <Stephan Gatzka>
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
#include <kernel.h>
#include <net/socket.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_server_socket.h"
#include "cio_socket.h"

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      cio_alloc_client alloc_client,
                                      cio_free_client free_client,
                                      uint64_t close_timeout_ns,
                                      cio_server_socket_close_hook close_hook)
{
	int listen_fd = zsock_socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (cio_unlikely(listen_fd == -1)) {
		return (enum cio_error)(-errno);
	}

	ss->impl.fd = listen_fd;
	ss->impl.close_timeout_ns = close_timeout_ns;
	ss->impl.loop = loop;

	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->close_hook = close_hook;
	ss->backlog = (int)backlog;

	return CIO_SUCCESS;
}

void cio_server_socket_close(struct cio_server_socket *ss)
{
	cio_zephyr_eventloop_remove_event(&ss->impl.ev);

	zsock_close(ss->impl.fd);
	if (ss->close_hook != NULL) {
		ss->close_hook(ss);
	}
}


