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
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_io_stream.h"
#include "cio_linux_socket.h"
#include "cio_socket.h"
#include "cio_util.h"
#include "linux/cio_linux_socket_utils.h"

static void socket_close(struct cio_socket *s)
{
	cio_linux_eventloop_remove(s->loop, &s->ev);

	close(s->ev.fd);
	if (s->close_hook != NULL) {
		s->close_hook(s);
	}
}

static enum cio_error socket_tcp_no_delay(struct cio_socket *s, bool on)
{
	int tcp_no_delay;

	if (on) {
		tcp_no_delay = 1;
	} else {
		tcp_no_delay = 0;
	}

	if (setsockopt(s->ev.fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
	               sizeof(tcp_no_delay)) < 0) {
		return (enum cio_error)errno;
	}

	return cio_success;
}

static enum cio_error socket_keepalive(struct cio_socket *s, bool on, unsigned int keep_idle_s,
                                       unsigned int keep_intvl_s, unsigned int keep_cnt)
{
	int keep_alive;

	if (on) {
		keep_alive = 1;
		if (setsockopt(s->ev.fd, SOL_TCP, TCP_KEEPIDLE, &keep_idle_s, sizeof(keep_idle_s)) == -1) {
			return (enum cio_error)errno;
		}

		if (setsockopt(s->ev.fd, SOL_TCP, TCP_KEEPINTVL, &keep_intvl_s, sizeof(keep_intvl_s)) == -1) {
			return (enum cio_error)errno;
		}

		if (setsockopt(s->ev.fd, SOL_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt)) == -1) {
			return (enum cio_error)errno;
		}
	} else {
		keep_alive = 0;
	}

	if (setsockopt(s->ev.fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) == -1) {
		return (enum cio_error)errno;
	}

	return cio_success;
}

static struct cio_io_stream *socket_get_io_stream(struct cio_socket *s)
{
	return &s->stream;
}

static void read_callback(void *context)
{
	struct cio_io_stream *stream = context;
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	ssize_t ret = read(s->ev.fd, stream->read_buffer, stream->read_count);
	if (ret == -1) {
		if (likely((errno == EWOULDBLOCK) || (errno == EAGAIN))) {
			return;
		} else {
			stream->read_handler(stream, stream->read_handler_context, (enum cio_error)errno, stream->read_buffer, 0);
		}
	} else {
		stream->read_handler(stream, stream->read_handler_context, cio_success, stream->read_buffer, (size_t)ret);
	}
}

static void stream_read(struct cio_io_stream *stream, void *buf, size_t count, cio_io_stream_read_handler handler, void *handler_context)
{
	enum cio_error err;
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	s->ev.context = stream;
	s->ev.read_callback = read_callback;
	s->stream.read_buffer = buf;
	s->stream.read_count = count;
	s->stream.read_handler = handler;
	s->stream.read_handler_context = handler_context;
	err = cio_linux_eventloop_register_read(s->loop, &s->ev);
	if (unlikely(err != cio_success)) {
		handler(stream, handler_context, err, buf, 0);
		return;
	}

	read_callback(stream);
}

static void write_callback(void *context)
{
	struct cio_io_stream *stream = context;
	stream->write_handler(stream, stream->write_handler_context, cio_success, 0);
}

static void stream_write(struct cio_io_stream *stream, const void *buf, size_t count, cio_io_stream_write_handler handler, void *handler_context)
{
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);

	ssize_t ret = send(s->ev.fd, buf, count, MSG_NOSIGNAL);
	if (likely(ret >= 0)) {
		handler(stream, handler_context, cio_success, (size_t)ret);
	} else {
		if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
			enum cio_error err;
			s->stream.write_handler = handler;
			s->stream.write_handler_context = handler_context;
			s->ev.context = stream;
			s->ev.write_callback = write_callback;
			err = cio_linux_eventloop_register_write(s->loop, &s->ev);
			if (unlikely(err != cio_success)) {
				handler(stream, handler_context, err, 0);
			}
		} else {
			handler(stream, handler_context, (enum cio_error)errno, 0);
		}
	}
}

static void stream_close(struct cio_io_stream *stream)
{
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	socket_close(s);
}

enum cio_error cio_linux_socket_init(struct cio_socket *s, int client_fd,
                                     struct cio_eventloop *loop,
                                     struct cio_allocator *allocator,
                                     cio_socket_close_hook close_hook)
{
	s->ev.fd = client_fd;
	s->allocator = allocator;
	s->ev.error_callback = NULL;
	s->ev.write_callback = NULL;
	s->ev.read_callback = NULL;
	s->ev.context = s;

	s->close = socket_close;
	s->set_tcp_no_delay = socket_tcp_no_delay;
	s->set_keep_alive = socket_keepalive;
	s->get_io_stream = socket_get_io_stream;

	s->stream.read_some = stream_read;
	s->stream.write_some = stream_write;
	s->stream.close = stream_close;

	s->loop = loop;
	s->close_hook = close_hook;

	return cio_linux_eventloop_add(s->loop, &s->ev);
}

enum cio_error cio_socket_init(struct cio_socket *s,
                               struct cio_eventloop *loop,
                               struct cio_allocator *allocator,
                               cio_socket_close_hook close_hook)
{
	enum cio_error err;
	int socket_fd = cio_linux_socket_create();
	if (unlikely(socket_fd == -1)) {
		return (enum cio_error)errno;
	}

	err = cio_linux_socket_init(s, socket_fd, loop, allocator, close_hook);
	if (unlikely(err != cio_success)) {
		close(socket_fd);
	}

	return err;
}
