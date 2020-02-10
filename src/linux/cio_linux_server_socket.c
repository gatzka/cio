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
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_endian.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_inet_address.h"
#include "cio_linux_socket.h"
#include "cio_server_socket.h"
#include "cio_socket.h"
#include "linux/cio_linux_socket_utils.h"
#include "os_config.h"

static void accept_callback(void *context, enum cio_epoll_error error)
{
	struct cio_server_socket *ss = context;

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		enum cio_error err = cio_linux_get_socket_error(ss->impl.ev.fd);
		ss->handler(ss, ss->handler_context, err, NULL);
		return;
	}

	struct sockaddr_storage addr;
	int fd = ss->impl.ev.fd;
	socklen_t addrlen;

	int client_fd;
	memset(&addr, 0, sizeof(addr));
	addrlen = sizeof(addr);
	client_fd = accept4(fd, (struct sockaddr *)&addr, &addrlen, (unsigned int)SOCK_NONBLOCK | (unsigned int)SOCK_CLOEXEC);
	if (cio_unlikely(client_fd == -1)) {
		if ((errno != EAGAIN) && (errno != EBADF)) {
			ss->handler(ss, ss->handler_context, (enum cio_error)(-errno), NULL);
		}
	} else {
		struct cio_socket *s = ss->alloc_client();
		if (cio_likely(s != NULL)) {
			enum cio_error err = cio_linux_socket_init(s, client_fd, ss->impl.loop, ss->impl.close_timeout_ns, ss->free_client);
			if (cio_likely(err == CIO_SUCCESS)) {
				ss->handler(ss, ss->handler_context, err, s);
			} else {
				ss->handler(ss, ss->handler_context, err, NULL);
				close(client_fd);
				ss->free_client(s);
			}
		} else {
			ss->handler(ss, ss->handler_context, CIO_NO_MEMORY, s);
			close(client_fd);
		}
	}
}

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      cio_alloc_client alloc_client,
                                      cio_free_client free_client,
                                      uint64_t close_timeout_ns,
                                      cio_server_socket_close_hook close_hook)
{
	int listen_fd = cio_linux_socket_create(CIO_INET6_ADDRESS);
	if (listen_fd == -1) {
		return (enum cio_error)(-errno);
	}

	ss->impl.ev.fd = listen_fd;
	ss->impl.close_timeout_ns = close_timeout_ns;

	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->impl.loop = loop;
	ss->close_hook = close_hook;
	ss->backlog = (int)backlog;
	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_accept(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	enum cio_error err;
	if (cio_unlikely(handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	ss->handler = handler;
	ss->handler_context = handler_context;
	ss->impl.ev.read_callback = accept_callback;
	ss->impl.ev.context = ss;

	int qlen = CONFIG_TCP_FASTOPEN_QUEUE_SIZE;
	setsockopt(ss->impl.ev.fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));
	if (cio_unlikely(listen(ss->impl.ev.fd, ss->backlog) < 0)) {
		return (enum cio_error)(-errno);
	}

	err = cio_linux_eventloop_add(ss->impl.loop, &ss->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	err = cio_linux_eventloop_register_read(ss->impl.loop, &ss->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}

void cio_server_socket_close(struct cio_server_socket *ss)
{
	cio_linux_eventloop_remove(ss->impl.loop, &ss->impl.ev);

	close(ss->impl.ev.fd);
	if (ss->close_hook != NULL) {
		ss->close_hook(ss);
	}
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

	int ret = bind(ss->impl.ev.fd, addr, addr_len);
	if (cio_unlikely(ret != 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_set_reuse_address(struct cio_server_socket *ss, bool on)
{
	int reuse;
	if (on) {
		reuse = 1;
	} else {
		reuse = 0;
	}

	if (cio_unlikely(setsockopt(ss->impl.ev.fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
	                            sizeof(reuse)) < 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}
