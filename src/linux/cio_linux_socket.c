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

#include <unistd.h>

#include "cio_util.h"
#include "linux/cio_linux_epoll.h"
#include "linux/cio_linux_socket.h"

static void socket_close(void *context)
{
	struct cio_socket *s = context;
	struct cio_linux_socket *ls = container_of(s, struct cio_linux_socket, socket);

	//	cio_linux_eventloop_remove(s->loop, &s->ev);

	close(ls->fd);
	if (ls->close != NULL) {
		ls->close(ls);
	}
}

struct cio_socket *cio_linux_socket_init(struct cio_linux_socket *s, int client_fd,
                                         struct cio_linux_eventloop_epoll *loop,
                                         cio_linux_socket_close_hook hook)
{
	s->fd = client_fd;
	s->socket.close = socket_close;
	s->loop = loop;
	s->close = hook;
	return &s->socket;
}
