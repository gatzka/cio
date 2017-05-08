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

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "cio_util.h"
#include "linux/cio_linux_epoll.h"
#include "linux/cio_linux_socket.h"

static void socket_close(void *context)
{
	struct cio_socket *s = context;
	struct cio_linux_socket *ls = container_of(s, struct cio_linux_socket, socket);

	cio_linux_eventloop_remove(ls->loop, &ls->ev);

	close(ls->ev.fd);
	if (ls->close != NULL) {
		ls->close(ls);
	}
}

static enum cio_error socket_tcp_no_delay(void *context, bool on)
{
	struct cio_socket *s = context;
	struct cio_linux_socket *ls = container_of(s, struct cio_linux_socket, socket);

	int tcp_no_delay;
	if(on) {
		tcp_no_delay = 1;
	} else {
		tcp_no_delay = 0;
	}

	if (setsockopt(ls->ev.fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
					sizeof(tcp_no_delay)) < 0) {
		return errno;
	}

	return cio_success;
}

struct cio_socket *cio_linux_socket_init(struct cio_linux_socket *ls, int client_fd,
                                         struct cio_linux_eventloop_epoll *loop,
                                         cio_linux_socket_close_hook hook)
{
	ls->ev.fd = client_fd;
	ls->socket.close = socket_close;
	ls->socket.set_tcp_no_delay = socket_tcp_no_delay;
	ls->loop = loop;
	ls->close = hook;
	return &ls->socket;
}
