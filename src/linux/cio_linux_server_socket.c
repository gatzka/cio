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
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_server_socket.h"
#include "linux/cio_linux_epoll.h"
#include "linux/cio_linux_server_socket.h"
#include "linux/cio_linux_socket.h"

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

static enum cio_error socket_init(void *context, unsigned int backlog)
{
	enum cio_error err;
	struct cio_linux_server_socket *lss = context;

	int listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (listen_fd == -1) {
		return errno;
	}

	err = set_fd_non_blocking(listen_fd);
	if (likely(err != cio_success)) {
		close(listen_fd);
		return errno;
	}

	lss->backlog = backlog;
	lss->ev.fd = listen_fd;

	return cio_success;
}

static void socket_close(void *context)
{
	struct cio_linux_server_socket *lss = context;

	cio_linux_eventloop_remove(lss->loop, &lss->ev);

	close(lss->ev.fd);
	if (lss->close != NULL) {
		lss->close(lss);
	}
}

static void free_linux_socket(struct cio_linux_socket *s)
{
	free(s);
}

static void accept_callback(void *context)
{
	int client_fd;
	struct sockaddr_storage addr;
	struct cio_linux_server_socket *ss = context;
	int fd = ss->ev.fd;
	socklen_t addrlen;

	while (1) {
		memset(&addr, 0, sizeof(addr));
		addrlen = sizeof(addr);
		client_fd = accept(fd, (struct sockaddr *)&addr, &addrlen);
		if (unlikely(client_fd == -1)) {
			if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EBADF)) {
				ss->handler(&ss->server_socket, ss->handler_context, errno, NULL);
			}

			return;
		} else {
			struct cio_linux_socket *ls = malloc(sizeof(*ls));
			if (likely(ls != NULL)) {
				struct cio_socket *s = cio_linux_socket_init(ls, client_fd, ss->loop, free_linux_socket);
				ss->handler(&ss->server_socket, ss->handler_context, cio_success, s);
			}
		}
	}
}

static enum cio_error socket_accept(void *context, cio_accept_handler handler, void *handler_context)
{
	enum cio_error err;
	struct cio_linux_server_socket *ss = context;
	if (unlikely(handler == NULL)) {
		return cio_invalid_argument;
	}

	ss->handler = handler;
	ss->handler_context = handler_context;
	ss->ev.read_callback = accept_callback;
	ss->ev.context = context;

	if (unlikely(listen(ss->ev.fd, ss->backlog) < 0)) {
		return errno;
	}

	err = cio_linux_eventloop_add(ss->loop, &ss->ev);
	if (unlikely(err != cio_success)) {
		return err;
	}

	accept_callback(context);
	return cio_success;
}

static enum cio_error socket_set_reuse_address(void *context, bool on)
{
	struct cio_linux_server_socket *ss = context;
	int reuse;
	if (on) {
		reuse = 1;
	} else {
		reuse = 0;
	}

	if (unlikely(setsockopt(ss->ev.fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
	                        sizeof(reuse)) < 0)) {
		return errno;
	}

	return cio_success;
}

static enum cio_error socket_bind(void *context, const char *bind_address, uint16_t port)
{
	struct cio_linux_server_socket *ss = context;

	struct addrinfo hints;
	char server_port_string[6];
	struct addrinfo *servinfo;
	int ret;
	struct addrinfo *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_V4MAPPED | AI_NUMERICHOST;

	snprintf(server_port_string, sizeof(server_port_string), "%d", port);

	if (bind_address == NULL) {
		bind_address = "::";
	}

	ret = getaddrinfo(bind_address, server_port_string, &hints, &servinfo);
	if (ret != 0) {
		switch (ret) {
		case EAI_SYSTEM:
			return errno;
		default:
			return cio_invalid_argument;
		}
	}

	for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
		if (likely(bind(ss->ev.fd, rp->ai_addr, rp->ai_addrlen) == 0)) {
			break;
		}
	}

	freeaddrinfo(servinfo);

	if (rp == NULL) {
		return cio_invalid_argument;
	}

	return cio_success;
}

const struct cio_server_socket *cio_linux_server_socket_init(struct cio_linux_server_socket *ss,
                                                             struct cio_linux_eventloop_epoll *loop,
                                                             cio_linux_server_socket_close_hook hook)
{
	ss->server_socket.context = ss;
	ss->server_socket.init = socket_init;
	ss->server_socket.close = socket_close;
	ss->server_socket.accept = socket_accept;
	ss->server_socket.set_reuse_address = socket_set_reuse_address;
	ss->server_socket.bind = socket_bind;
	ss->loop = loop;
	ss->close = hook;
	ss->backlog = 0;
	return &ss->server_socket;
}
