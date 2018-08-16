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

#define _GNU_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_io_stream.h"
#include "cio_linux_socket.h"
#include "cio_read_buffer.h"
#include "cio_socket.h"
#include "cio_util.h"
#include "cio_write_buffer.h"
#include "linux/cio_linux_socket_utils.h"

static enum cio_error socket_close(struct cio_socket *s)
{
	if (unlikely(s == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	cio_linux_eventloop_remove(s->loop, &s->ev);

	close(s->ev.fd);
	if (s->close_hook != NULL) {
		s->close_hook(s);
	}

	return CIO_SUCCESS;
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
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

static enum cio_error socket_keepalive(struct cio_socket *s, bool on, unsigned int keep_idle_s,
                                       unsigned int keep_intvl_s, unsigned int keep_cnt)
{
	int keep_alive;

	if (on) {
		keep_alive = 1;
		if (setsockopt(s->ev.fd, SOL_TCP, TCP_KEEPIDLE, &keep_idle_s, sizeof(keep_idle_s)) == -1) {
			return (enum cio_error)(-errno);
		}

		if (setsockopt(s->ev.fd, SOL_TCP, TCP_KEEPINTVL, &keep_intvl_s, sizeof(keep_intvl_s)) == -1) {
			return (enum cio_error)(-errno);
		}

		if (setsockopt(s->ev.fd, SOL_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt)) == -1) {
			return (enum cio_error)(-errno);
		}
	} else {
		keep_alive = 0;
	}

	if (setsockopt(s->ev.fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) == -1) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

static struct cio_io_stream *socket_get_io_stream(struct cio_socket *s)
{
	return &s->stream;
}

static void read_callback(void *context)
{
	struct cio_io_stream *stream = context;
	struct cio_read_buffer *rb = stream->read_buffer;
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	ssize_t ret = read(s->ev.fd, rb->add_ptr, cio_read_buffer_space_available(rb));
	if (ret == -1) {
		if (unlikely((errno != EWOULDBLOCK) && (errno != EAGAIN))) {
			rb->bytes_transferred = 0;
			stream->read_handler(stream, stream->read_handler_context, (enum cio_error)(-errno), rb);
		}
	} else {
		enum cio_error error;
		rb->bytes_transferred = (size_t)ret;
		if (ret == 0) {
			error = CIO_EOF;
		} else {
			rb->add_ptr += (size_t)ret;
			error = CIO_SUCCESS;
		}

		stream->read_handler(stream, stream->read_handler_context, error, rb);
	}
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context)
{
	if (unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	s->ev.context = stream;
	s->ev.read_callback = read_callback;
	s->stream.read_buffer = buffer;
	s->stream.read_handler = handler;
	s->stream.read_handler_context = handler_context;
	return cio_linux_eventloop_register_read(s->loop, &s->ev);
}

static void write_callback(void *context)
{
	struct cio_io_stream *stream = context;
	stream->write_handler(stream, stream->write_handler_context, stream->write_buffer, CIO_SUCCESS, 0);
}

static enum cio_error stream_write(struct cio_io_stream *stream, const struct cio_write_buffer *buffer, cio_io_stream_write_handler handler, void *handler_context)
{
	if (unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	size_t chain_length = cio_write_buffer_get_number_of_elements(buffer);
	struct iovec msg_iov[chain_length];
	struct msghdr msg;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	msg.msg_iov = msg_iov;
	msg.msg_iovlen = chain_length;

	struct cio_write_buffer *wb = buffer->next;
	for (unsigned int i = 0; i < chain_length; i++) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
		msg_iov[i].iov_base = (void *)wb->data.element.const_data;
#pragma GCC diagnostic pop
		msg_iov[i].iov_len = wb->data.element.length;
		wb = wb->next;
	}

	ssize_t ret = sendmsg(s->ev.fd, &msg, MSG_NOSIGNAL);
	if (likely(ret >= 0)) {
		handler(stream, handler_context, buffer, CIO_SUCCESS, (size_t)ret);
	} else {
		if (likely((errno == EWOULDBLOCK) || (errno == EAGAIN))) {
			s->stream.write_handler = handler;
			s->stream.write_handler_context = handler_context;
			s->stream.write_buffer = buffer;
			s->ev.context = stream;
			s->ev.write_callback = write_callback;
			return cio_linux_eventloop_register_write(s->loop, &s->ev);
		}

		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	return socket_close(s);
}

enum cio_error cio_linux_socket_init(struct cio_socket *s, int client_fd,
                                     struct cio_eventloop *loop,
                                     cio_socket_close_hook close_hook)
{
	if (unlikely((s == NULL) || (loop == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	s->ev.fd = client_fd;
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
                               cio_socket_close_hook close_hook)
{
	enum cio_error err;
	int socket_fd = cio_linux_socket_create();
	if (unlikely(socket_fd == -1)) {
		return (enum cio_error)(-errno);
	}

	err = cio_linux_socket_init(s, socket_fd, loop, close_hook);
	if (unlikely(err != CIO_SUCCESS)) {
		close(socket_fd);
	}

	return err;
}
