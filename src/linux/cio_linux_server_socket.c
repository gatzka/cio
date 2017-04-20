/*
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
#include <fcntl.h>
#include <netdb.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_linux_epoll.h"
#include "cio_linux_server_socket.h"
#include "cio_server_socket.h"

static enum cio_error set_fd_non_blocking(int fd)
{
	int fd_flags = fcntl(fd, F_GETFL, 0);
	if (unlikely(fd_flags < 0)) {
		return errno;
	}

	fd_flags |= O_NONBLOCK;
	if (unlikely(fcntl(fd, F_SETFL, fd_flags) < 0)) {
		return errno;
	}

	return cio_success;
}

static enum cio_error socket_init(void *context, uint16_t port, unsigned int backlog, const char *bind_address)
{
	struct addrinfo hints = {0};

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_V4MAPPED | AI_NUMERICHOST;

	char server_port_string[6];
	snprintf(server_port_string, sizeof(server_port_string), "%d", port);

	if (bind_address == NULL) {
		bind_address = "::";
	}

	struct addrinfo *servinfo;
	int ret = getaddrinfo(bind_address, server_port_string, &hints, &servinfo);
	if (ret != 0) {
		switch (ret) {
			case EAI_SYSTEM:
				return errno;
			default:
				return cio_invalid_argument;
		}
	}

	int listen_fd = -1;
	struct addrinfo *rp;
	for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
		listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (listen_fd == -1) {
			continue;
		}

		static const int reuse_on = 1;
		if (likely(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_on,
								sizeof(reuse_on)) == 0)) {
			enum cio_error err = set_fd_non_blocking(listen_fd);
			if (likely(err == cio_success)) {
				if (likely(bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0)) {
					break;
				}
			}
		}

		close(listen_fd);
	}

	freeaddrinfo(servinfo);

	if ((listen_fd == -1) || (rp == NULL)) {
		return cio_invalid_argument;
	}

	if (unlikely(listen(listen_fd, backlog) < 0)) {
		close(listen_fd);
		return errno;
	}

	struct cio_linux_server_socket *ss = context;
	ss->fd = listen_fd;

	return cio_success;
}

static void socket_close(void *context)
{
	struct cio_linux_server_socket *ss = context;

	cio_linux_eventloop_remove(ss->loop, &ss->ev);

	close(ss->fd);
	if (ss->close != NULL) {
		ss->close(ss);
	}
}

static void accept_callback(void *context)
{
	struct cio_linux_server_socket *ss = context;
	int fd = ss->fd;

	while (1) {
		struct sockaddr_storage addr = {0};
		socklen_t addrlen = sizeof(addr);
		int client_fd = accept(fd, (struct sockaddr *)&addr, &addrlen);
		if (unlikely(client_fd == -1)) {
			if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EBADF)) {
				ss->handler(&ss->server_socket, ss->handler_context, errno, NULL);
				return;
			} else {
				return;
			}
		} else {
			//TODO: create client socket and pass it instead of NULL
			ss->handler(&ss->server_socket, ss->handler_context, cio_success, NULL);
		}
	}
}

static enum cio_error socket_accept(void *context, cio_accept_handler handler, void *handler_context)
{
	struct cio_linux_server_socket *ss = context;
	if (unlikely(handler == NULL)) {
		return cio_invalid_argument;
	}

	ss->handler = handler;
	ss->handler_context = handler_context;
	ss->ev.callback = accept_callback;
	ss->ev.context = context;
	ss->ev.fd = ss->fd;
	enum cio_error err = cio_linux_eventloop_add(ss->loop, &ss->ev);
	if (unlikely(err != cio_success)) {
		return err;
	}

	accept_callback(context);
	return cio_success;
}

const struct cio_server_socket *cio_linux_server_socket_init(struct cio_linux_server_socket *ss,
                                                             struct cio_linux_eventloop_epoll *loop,
                                                             close_hook close) {
	ss->server_socket.context = ss;
	ss->server_socket.init = socket_init;
	ss->server_socket.close = socket_close;
	ss->server_socket.accept = socket_accept;
	ss->fd = -1;
	ss->loop = loop;
	ss->close = close;
	return &ss->server_socket;
}
