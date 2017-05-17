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

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_io_stream.h"
#include "cio_socket.h"
#include "linux/cio_linux_epoll.h"
#include "linux/cio_linux_socket.h"
#include "linux/cio_linux_socket_utils.h"

static void socket_close(void *context)
{
	struct cio_linux_socket *ls = context;

	cio_linux_eventloop_remove(ls->loop, &ls->ev);

	close(ls->ev.fd);
	if (ls->close != NULL) {
		ls->close(ls);
	}
}

static enum cio_error socket_tcp_no_delay(void *context, bool on)
{
	struct cio_linux_socket *ls = context;
	int tcp_no_delay;

	if (on) {
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

static enum cio_error socket_keepalive(void *context, bool on, unsigned int keep_idle_s,
                                       unsigned int keep_intvl_s, unsigned int keep_cnt)
{
	struct cio_linux_socket *ls = context;
	int keep_alive;

	if (on) {
		keep_alive = 1;
		if (setsockopt(ls->ev.fd, SOL_TCP, TCP_KEEPIDLE, &keep_idle_s, sizeof(keep_idle_s)) == -1) {
			return errno;
		}

		if (setsockopt(ls->ev.fd, SOL_TCP, TCP_KEEPINTVL, &keep_intvl_s, sizeof(keep_intvl_s)) == -1) {
			return errno;
		}

		if (setsockopt(ls->ev.fd, SOL_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt)) == -1) {
			return errno;
		}
	} else {
		keep_alive = 0;
	}

	if (setsockopt(ls->ev.fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) == -1) {
		return errno;
	}

	return cio_success;
}

static struct cio_io_stream *socket_get_io_stream(void *context)
{
	struct cio_linux_socket *ls = context;
	return &ls->stream;
}

static void read_callback(void *context)
{
	struct cio_linux_socket *ls = context;
	ssize_t ret = read(ls->ev.fd, ls->read_buffer, ls->read_count);
	if (ret == -1) {
		if (likely((errno == EWOULDBLOCK) || (errno == EAGAIN))) {
			return;
		} else {
			ls->read_handler(ls->read_handler_context, errno, ls->read_buffer, 0);
		}
	} else {
		ls->read_handler(ls->read_handler_context, cio_success, ls->read_buffer, (size_t)ret);
	}
}

static void socket_read(void *context, void *buf, size_t count, cio_stream_read_handler handler, void *handler_context)
{
	enum cio_error err;
	struct cio_linux_socket *ls = context;
	ls->ev.context = ls;
	ls->ev.read_callback = read_callback;
	ls->read_buffer = buf;
	ls->read_count = count;
	ls->read_handler = handler;
	ls->read_handler_context = handler_context;
	err = cio_linux_eventloop_register_read(ls->loop, &ls->ev);
	if (unlikely(err != cio_success)) {
		handler(handler_context, err, buf, 0);
		return;
	}

	read_callback(ls);
}

static void write_callback(void *context)
{
	struct cio_linux_socket *ls = context;
	ls->write_handler(ls->write_handler_context, cio_success, 0);
}

static void socket_writev(void *context, struct cio_io_vector *io_vec, unsigned int count, cio_stream_write_handler handler, void *handler_context)
{
	struct iovec *iov = (struct iovec *)io_vec;
	struct cio_linux_socket *ls = context;
	struct msghdr message = {
	    .msg_name = NULL,
	    .msg_namelen = 0,
	    .msg_iov = iov,
	    .msg_iovlen = count,
	    .msg_control = NULL,
	    .msg_controllen = 0,
	    .msg_flags = 0};

	ssize_t ret = sendmsg(ls->ev.fd, &message, MSG_NOSIGNAL);
	if (likely(ret >= 0)) {
		handler(handler_context, cio_success, (size_t)ret);
	} else {
		if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
			enum cio_error err;
			ls->write_handler = handler;
			ls->write_handler_context = handler_context;
			ls->ev.context = ls;
			ls->ev.write_callback = write_callback;
			err = cio_linux_eventloop_register_write(ls->loop, &ls->ev);
			if (unlikely(err != cio_success)) {
				handler(handler_context, err, 0);
			}
		} else {
			handler(handler_context, errno, 0);
		}
	}
}

static void loop_callback(void *context)
{
	struct cio_linux_socket *ls = context;
	(void)ls;
}

struct cio_socket *cio_socket_init(struct cio_linux_socket *ls, int client_fd,
                                         struct cio_eventloop *loop,
                                         cio_socket_close_hook hook)
{
	enum cio_error err = set_fd_non_blocking(client_fd);
	if (unlikely(err != cio_success)) {
		return NULL;
	}

	ls->ev.fd = client_fd;
	ls->ev.read_callback = loop_callback;
	ls->ev.context = ls;

	ls->socket.context = ls;
	ls->socket.close = socket_close;
	ls->socket.set_tcp_no_delay = socket_tcp_no_delay;
	ls->socket.set_keep_alive = socket_keepalive;
	ls->socket.get_io_stream = socket_get_io_stream;

	ls->stream.context = ls;
	ls->stream.read_some = socket_read;
	ls->stream.writev_some = socket_writev;
	ls->stream.close = socket_close;

	ls->loop = loop;
	ls->close = hook;

	cio_linux_eventloop_add(ls->loop, &ls->ev);
	return &ls->socket;
}
