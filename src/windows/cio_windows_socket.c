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

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_socket.h"
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
	if cio_unlikely(WSAIoctl((SOCKET)s->ev.fd, SIO_KEEPALIVE_VALS, &alive, sizeof(alive), NULL, 0, &bytes_returned, NULL, NULL) == SOCKET_ERROR) {
		return (enum cio_error)(-WSAGetLastError());
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
	//s->get_io_stream = socket_get_io_stream;

	//s->stream.read_some = stream_read;
	//s->stream.write_some = stream_write;
	//s->stream.close = stream_close;

	return cio_windows_eventloop_add(&s->ev, loop);
}
