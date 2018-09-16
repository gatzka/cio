/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2018> <Stephan Gatzka>
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

#include <WinSock2.h>
#include <Mstcpip.h>
#include <Windows.h>
#include <malloc.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_socket.h"
#include "cio_util.h"
#include "cio_windows_socket.h"

static enum cio_error socket_close(struct cio_socket *s)
{
	if (cio_unlikely(s == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	cio_windows_eventloop_remove(&s->ev);

	closesocket((SOCKET)s->ev.fd);
	if (s->close_hook != NULL) {
		s->close_hook(s);
	}

	return CIO_SUCCESS;
}

static enum cio_error socket_tcp_no_delay(struct cio_socket *s, bool on)
{
	char tcp_no_delay = (char)on;

	if (cio_unlikely(setsockopt((SOCKET)s->ev.fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
	                            sizeof(tcp_no_delay)) == SOCKET_ERROR)) {
		return (enum cio_error)(-WSAGetLastError());
	}

	return CIO_SUCCESS;
}

static enum cio_error socket_keepalive(struct cio_socket *s, bool on, unsigned int keep_idle_s,
                                       unsigned int keep_intvl_s, unsigned int keep_cnt)
{
	(void)keep_cnt;

	struct tcp_keepalive alive = {.onoff = on, .keepalivetime = keep_idle_s, .keepaliveinterval = keep_intvl_s};

	DWORD bytes_returned;
	if
		cio_unlikely(WSAIoctl((SOCKET)s->ev.fd, SIO_KEEPALIVE_VALS, &alive, sizeof(alive), NULL, 0, &bytes_returned, NULL, NULL) == SOCKET_ERROR)
		{
			return (enum cio_error)(-WSAGetLastError());
		}

	return CIO_SUCCESS;
}

static struct cio_io_stream *socket_get_io_stream(struct cio_socket *s)
{
	return &s->stream;
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	return socket_close(s);
}

static void read_callback(void *context)
{
	int error;

	struct cio_io_stream *stream = context;
	struct cio_read_buffer *rb = stream->read_buffer;
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);

	DWORD recv_bytes;
	DWORD flags = 0;
	BOOL rc = WSAGetOverlappedResult((SOCKET)s->ev.fd, &s->ev.overlapped, &recv_bytes, FALSE, &flags);
	if (cio_unlikely(rc == FALSE)) {
		error = WSAGetLastError();
		stream->read_handler(stream, stream->read_handler_context, (enum cio_error)(-error), rb);
		return;
	}

	rb->bytes_transferred = (size_t)recv_bytes;
	if (recv_bytes == 0) {
		error = CIO_EOF;
	} else {
		rb->add_ptr += (size_t)recv_bytes;
		error = CIO_SUCCESS;
	}

	stream->read_handler(stream, stream->read_handler_context, error, rb);
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = container_of(stream, struct cio_socket, stream);

	s->stream.read_buffer = buffer;
	s->stream.read_handler = handler;
	s->stream.read_handler_context = handler_context;
	s->ev.context = stream;
	s->ev.callback = read_callback;

	WSABUF wsa_buffer;
	wsa_buffer.len = (ULONG)cio_read_buffer_space_available(buffer);
	wsa_buffer.buf = (CHAR *)buffer->add_ptr;
	DWORD received_bytes;
	DWORD flags = 0;
	int error;

	int rc = WSARecv((SOCKET)s->ev.fd, &wsa_buffer, 1, &received_bytes, &flags, &s->ev.overlapped, NULL);
	if (rc == SOCKET_ERROR) {
		error = WSAGetLastError();
		if (cio_likely(error == WSA_IO_PENDING)) {
			return CIO_SUCCESS;
		} else {
			return (enum cio_error)(-error);
		}
	} else {
		buffer->bytes_transferred = (size_t)received_bytes;
		if (received_bytes == 0) {
			error = CIO_EOF;
		} else {
			buffer->add_ptr += (size_t)received_bytes;
			error = CIO_SUCCESS;
		}

		stream->read_handler(stream, stream->read_handler_context, error, buffer);
		return CIO_SUCCESS;
	}
}

static void write_callback(void *context)
{
	int error;

	struct cio_io_stream *stream = context;
	struct cio_socket *s = container_of(stream, struct cio_socket, stream);

	DWORD bytes_sent;
	DWORD flags = 0;
	BOOL rc = WSAGetOverlappedResult((SOCKET)s->ev.fd, &s->ev.overlapped, &bytes_sent, FALSE, &flags);
	if (cio_unlikely(rc == FALSE)) {
		error = WSAGetLastError();
		stream->write_handler(stream, stream->read_handler_context, stream->write_buffer, (enum cio_error)(-error), 0);
		return;
	}

	stream->write_handler(stream, stream->write_handler_context, stream->write_buffer, CIO_SUCCESS, (size_t)bytes_sent);
}

static enum cio_error stream_write(struct cio_io_stream *stream, const struct cio_write_buffer *buffer, cio_io_stream_write_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = container_of(stream, struct cio_socket, stream);
	size_t chain_length = cio_write_buffer_get_number_of_elements(buffer);

	DWORD bytes_sent;
	WSABUF *wsa_buffers = alloca(sizeof(*wsa_buffers) * chain_length);
	struct cio_write_buffer *wb = buffer->next;
	for (size_t i = 0; i < chain_length; i++) {
		wsa_buffers[i].buf = (void *)wb->data.element.const_data;
		wsa_buffers[i].len = (ULONG)wb->data.element.length;
		wb = wb->next;
	}

	int rc = WSASend((SOCKET)s->ev.fd, wsa_buffers, (DWORD)chain_length, &bytes_sent, 0, &s->ev.overlapped, NULL);
	if (rc == SOCKET_ERROR) {
		int error = WSAGetLastError();
		if (cio_likely(error == WSA_IO_PENDING)) {
			s->stream.write_handler = handler;
			s->stream.write_handler_context = handler_context;
			s->stream.write_buffer = buffer;
			s->ev.context = stream;
			s->ev.callback = write_callback;
			return CIO_SUCCESS;
		} else {
			return (enum cio_error)(-error);
		}
	} else {
		handler(stream, handler_context, buffer, CIO_SUCCESS, (size_t)bytes_sent);
		return CIO_SUCCESS;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_windows_socket_init(struct cio_socket *s, SOCKET client_fd,
                                       struct cio_eventloop *loop,
                                       cio_socket_close_hook close_hook)
{
	if (cio_unlikely((s == NULL) || (loop == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	s->ev.fd = (HANDLE)client_fd;
	s->ev.callback = NULL;
	s->ev.context = s;

	s->loop = loop;
	s->close_hook = close_hook;

	s->close = socket_close;
	s->set_tcp_no_delay = socket_tcp_no_delay;
	s->set_keep_alive = socket_keepalive;
	s->get_io_stream = socket_get_io_stream;

	s->stream.read_some = stream_read;
	s->stream.write_some = stream_write;
	s->stream.close = stream_close;

	return cio_windows_eventloop_add(&s->ev, loop);
}
