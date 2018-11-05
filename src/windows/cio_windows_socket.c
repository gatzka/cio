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

static void try_free(struct cio_socket *s)
{
	if ((s->impl.read_event.overlapped_operations_in_use == 0) && (s->impl.write_event.overlapped_operations_in_use == 0)) {
		closesocket((SOCKET)s->impl.fd);
		if (s->close_hook != NULL) {
			s->close_hook(s);
		}
	}
}

static enum cio_error socket_close(struct cio_socket *s)
{
	if (cio_unlikely(s == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	CancelIo(s->impl.fd);
	shutdown((SOCKET)s->impl.fd, SD_BOTH);
	try_free(s);

	return CIO_SUCCESS;
}

static enum cio_error socket_tcp_no_delay(struct cio_socket *s, bool on)
{
	char tcp_no_delay = (char)on;

	if (cio_unlikely(setsockopt((SOCKET)s->impl.fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
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
	if (cio_unlikely(WSAIoctl((SOCKET)s->impl.fd, SIO_KEEPALIVE_VALS, &alive, sizeof(alive), NULL, 0, &bytes_returned, NULL, NULL) == SOCKET_ERROR)) {
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
	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
	return socket_close(s);
}

static void read_callback(struct cio_event_notifier *ev)
{
	struct cio_socket_impl *impl = cio_container_of(ev, struct cio_socket_impl, read_event);
	struct cio_socket *s = cio_container_of(impl, struct cio_socket, impl);

	DWORD recv_bytes;
	DWORD flags = 0;
	BOOL rc = WSAGetOverlappedResult((SOCKET)impl->fd, &ev->overlapped, &recv_bytes, FALSE, &flags);
	ev->overlapped_operations_in_use--;
	enum cio_error error_code;
	if (cio_unlikely(rc == FALSE)) {
		int error = WSAGetLastError();
		if (error == WSA_OPERATION_ABORTED) {
			try_free(s);
			return;
		}

		error_code = (enum cio_error)(-error);
	} else {
		s->stream.read_buffer->bytes_transferred = (size_t)recv_bytes;
		if (recv_bytes == 0) {
			error_code = CIO_EOF;
		} else {
			s->stream.read_buffer->add_ptr += (size_t)recv_bytes;
			error_code = CIO_SUCCESS;
		}
	}

	s->stream.read_handler(&s->stream, s->stream.read_handler_context, error_code, s->stream.read_buffer);
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);

	s->stream.read_buffer = buffer;
	s->stream.read_handler = handler;
	s->stream.read_handler_context = handler_context;

	WSABUF wsa_buffer;
	wsa_buffer.len = (ULONG)cio_read_buffer_space_available(buffer);
	wsa_buffer.buf = (CHAR *)buffer->add_ptr;
	DWORD flags = 0;

	memset(&s->impl.read_event.overlapped, 0, sizeof(s->impl.read_event.overlapped));

	int rc = WSARecv((SOCKET)s->impl.fd, &wsa_buffer, 1, NULL, &flags, &s->impl.read_event.overlapped, NULL);
	if (rc == SOCKET_ERROR) {
		int error = WSAGetLastError();
		if (cio_unlikely(error != WSA_IO_PENDING)) {
			return (enum cio_error)(-error);
		}
	}

	s->impl.read_event.overlapped_operations_in_use++;
	return CIO_SUCCESS;
}

static void write_callback(struct cio_event_notifier *ev)
{
	struct cio_socket_impl *impl = cio_container_of(ev, struct cio_socket_impl, write_event);
	struct cio_socket *s = cio_container_of(impl, struct cio_socket, impl);

	DWORD bytes_sent;
	DWORD flags = 0;
	BOOL rc = WSAGetOverlappedResult((SOCKET)impl->fd, &ev->overlapped, &bytes_sent, FALSE, &flags);
	ev->overlapped_operations_in_use--;
	enum cio_error error_code = CIO_SUCCESS;
	if (cio_unlikely(rc == FALSE)) {
		int error = WSAGetLastError();
		if (error == WSA_OPERATION_ABORTED) {
			try_free(s);
			return;
		}

		bytes_sent = 0;
		error_code = (enum cio_error)(-error);
	}

	s->stream.write_handler(&s->stream, s->stream.write_handler_context, s->stream.write_buffer, error_code, (size_t)bytes_sent);
}

static enum cio_error stream_write(struct cio_io_stream *stream, const struct cio_write_buffer *buffer, cio_io_stream_write_handler handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_socket *s = cio_container_of(stream, struct cio_socket, stream);
	s->stream.write_handler = handler;
	s->stream.write_handler_context = handler_context;
	s->stream.write_buffer = buffer;

	size_t chain_length = cio_write_buffer_get_number_of_elements(buffer);

	WSABUF *wsa_buffers = alloca(sizeof(*wsa_buffers) * chain_length);
	struct cio_write_buffer *wb = buffer->next;
	for (size_t i = 0; i < chain_length; i++) {
		wsa_buffers[i].buf = (void *)wb->data.element.const_data;
		wsa_buffers[i].len = (ULONG)wb->data.element.length;
		wb = wb->next;
	}

	memset(&s->impl.write_event.overlapped, 0, sizeof(s->impl.write_event.overlapped));

	int rc = WSASend((SOCKET)s->impl.fd, wsa_buffers, (DWORD)chain_length, NULL, 0, &s->impl.write_event.overlapped, NULL);
	if (rc == SOCKET_ERROR) {
		int error = WSAGetLastError();
		if (cio_unlikely(error != WSA_IO_PENDING)) {
			return (enum cio_error)(-error);
		}
	}

	s->impl.write_event.overlapped_operations_in_use++;
	return CIO_SUCCESS;
}

enum cio_error cio_windows_socket_init(struct cio_socket *s, SOCKET client_fd,
                                       struct cio_eventloop *loop,
                                       cio_socket_close_hook close_hook)
{
	if (cio_unlikely((s == NULL) || (loop == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	s->impl.fd = (HANDLE)client_fd;

	s->impl.read_event.callback = read_callback;
	s->impl.read_event.overlapped_operations_in_use = 0;
	s->impl.write_event.callback = write_callback;
	s->impl.write_event.overlapped_operations_in_use = 0;
	s->impl.loop = loop;
	s->close_hook = close_hook;

	s->close = socket_close;
	s->set_tcp_no_delay = socket_tcp_no_delay;
	s->set_keep_alive = socket_keepalive;
	s->get_io_stream = socket_get_io_stream;

	s->stream.read_some = stream_read;
	s->stream.write_some = stream_write;
	s->stream.close = stream_close;

	enum cio_error err = cio_windows_add_handle_to_completion_port((HANDLE)client_fd, loop, &s->stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}
