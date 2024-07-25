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

#include "cio/compiler.h"
#include "cio/endian.h"
#include "cio/error_code.h"
#include "cio/eventloop_impl.h"
#include "cio/inet_address.h"
#include "cio/linux_socket.h"
#include "cio/linux_socket_utils.h"
#include "cio/os_config.h"
#include "cio/server_socket.h"
#include "cio/socket.h"
#include "cio/socket_address.h"

static void accept_callback(void *context, enum cio_epoll_error error)
{
	struct cio_server_socket *server_socket = context;

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		enum cio_error err = cio_linux_get_socket_error(server_socket->impl.ev.fd);
		server_socket->handler(server_socket, server_socket->handler_context, err, NULL);
		return;
	}

	int fd = server_socket->impl.ev.fd;

	struct sockaddr_storage addr;
	memset(&addr, 0, sizeof(addr));
	socklen_t addrlen = sizeof(addr);

	int client_fd = accept4(fd, (struct sockaddr *)&addr, &addrlen, (unsigned int)SOCK_NONBLOCK | (unsigned int)SOCK_CLOEXEC);
	if (cio_unlikely(client_fd == -1)) {
		if ((errno != EAGAIN) && (errno != EBADF)) {
			server_socket->handler(server_socket, server_socket->handler_context, (enum cio_error)(-errno), NULL);
		}
	} else {
		struct cio_socket *client_socket = server_socket->alloc_client();
		if (cio_unlikely(client_socket == NULL)) {
			server_socket->handler(server_socket, server_socket->handler_context, CIO_NO_MEMORY, client_socket);
			close(client_fd);
			return;
		}

		enum cio_error err = cio_linux_socket_init(client_socket, client_fd, server_socket->impl.loop, server_socket->impl.close_timeout_ns, server_socket->free_client);
		if (cio_likely(err == CIO_SUCCESS)) {
			server_socket->handler(server_socket, server_socket->handler_context, err, client_socket);
		} else {
			server_socket->handler(server_socket, server_socket->handler_context, err, NULL);
			close(client_fd);
			server_socket->free_client(client_socket);
		}
	}
}

enum cio_error cio_server_socket_init(struct cio_server_socket *server_socket,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      enum cio_address_family family,
                                      cio_alloc_client_t alloc_client,
                                      cio_free_client_t free_client,
                                      uint64_t close_timeout_ns,
                                      cio_server_socket_close_hook_t close_hook)
{
	int listen_fd = cio_linux_socket_create(family);
	if (cio_unlikely(listen_fd == -1)) {
		return (enum cio_error)(-errno);
	}

	server_socket->impl.ev.fd = listen_fd;
	server_socket->impl.close_timeout_ns = close_timeout_ns;

	server_socket->alloc_client = alloc_client;
	server_socket->free_client = free_client;
	server_socket->impl.loop = loop;
	server_socket->close_hook = close_hook;
	server_socket->backlog = (int)backlog;
	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_accept(struct cio_server_socket *server_socket, cio_accept_handler_t handler, void *handler_context)
{
	enum cio_error err = CIO_SUCCESS;
	if (cio_unlikely(handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	server_socket->handler = handler;
	server_socket->handler_context = handler_context;
	server_socket->impl.ev.read_callback = accept_callback;
	server_socket->impl.ev.context = server_socket;

#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-fd-leak"
#endif
	int ret = listen(server_socket->impl.ev.fd, server_socket->backlog);
	if (cio_unlikely(ret < 0)) {
		return (enum cio_error)(-errno);
	}

	err = cio_linux_eventloop_add(server_socket->impl.loop, &server_socket->impl.ev);
#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic pop
#endif
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	err = cio_linux_eventloop_register_read(server_socket->impl.loop, &server_socket->impl.ev);
	return err;
}

void cio_server_socket_close(struct cio_server_socket *server_socket)
{
	cio_linux_eventloop_remove(server_socket->impl.loop, &server_socket->impl.ev);

	close(server_socket->impl.ev.fd);
	if (server_socket->close_hook != NULL) {
		server_socket->close_hook(server_socket);
	}
}

static bool is_standard_uds_socket(const struct cio_socket_address *endpoint)
{
	if (endpoint->impl.sa.socket_address.addr.sa_family != (sa_family_t)CIO_ADDRESS_FAMILY_UNIX) {
		return false;
	}

	if (endpoint->impl.sa.unix_address.un.sun_path[0] == '\0') {
		return false;
	}

	return true;
}

static enum cio_error try_removing_uds_file(const struct cio_socket_address *endpoint)
{
	int ret = unlink(endpoint->impl.sa.unix_address.un.sun_path);
	enum cio_error err = CIO_SUCCESS;
	if (cio_unlikely(ret == -1)) {
		err = (enum cio_error)(-errno);
		if (cio_likely(err == CIO_NO_SUCH_FILE_OR_DIRECTORY)) {
			err = CIO_SUCCESS;
		}
	}

	return err;
}

enum cio_error cio_server_socket_bind(struct cio_server_socket *server_socket, const struct cio_socket_address *endpoint)
{
	if (cio_unlikely((server_socket == NULL) || (endpoint == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely((enum cio_address_family)endpoint->impl.sa.socket_address.addr.sa_family == CIO_ADDRESS_FAMILY_UNSPEC)) {
		return CIO_INVALID_ARGUMENT;
	}

	if (is_standard_uds_socket(endpoint)) {
		enum cio_error err = try_removing_uds_file(endpoint);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			return err;
		}
	}

	const struct sockaddr *addr = &endpoint->impl.sa.socket_address.addr;
	socklen_t addr_len = endpoint->impl.len;

	int ret = bind(server_socket->impl.ev.fd, addr, addr_len);
	if (cio_unlikely(ret != 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_set_reuse_address(const struct cio_server_socket *server_socket, bool on)
{
	int reuse = (int)on;

	if (cio_unlikely(setsockopt(server_socket->impl.ev.fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
	                            sizeof(reuse)) < 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_set_tcp_fast_open(const struct cio_server_socket *server_socket, bool on)
{
	int qlen = 0;
	if (on) {
		qlen = CONFIG_TCP_FASTOPEN_QUEUE_SIZE;
	}

	int ret = setsockopt(server_socket->impl.ev.fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));
	if (cio_unlikely(ret != 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}
