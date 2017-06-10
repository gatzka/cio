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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_io_stream.h"
#include "cio_server_socket.h"
#include "cio_socket.h"

static struct cio_eventloop loop;

static uint8_t buffer[100];

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred);

static void handle_write(struct cio_io_stream *stream, void *handler_context, enum cio_error err, size_t bytes_transferred)
{
	(void)handler_context;
	(void)bytes_transferred;
	if (err != cio_success) {
		fprintf(stderr, "write error!\n");
		return;
	}

	stream->read_some(stream, buffer, sizeof(buffer), handle_read, NULL);
}

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred)
{
	(void)handler_context;
	if (err != cio_success) {
		fprintf(stderr, "read error!\n");
		return;
	}

	stream->write_some(stream, buf, bytes_transferred, handle_write, NULL);
}

static void handle_accept(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)ss;
	(void)handler_context;
	if (err != cio_success) {
		fprintf(stderr, "accept error!\n");
		return;
	}

	struct cio_io_stream *stream = socket->get_io_stream(socket);
	stream->read_some(stream, buffer, sizeof(buffer), handle_read, NULL);
}

int main()
{
	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		return -1;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		signal(SIGTERM, SIG_DFL);
		return -1;
	}

	enum cio_error err = cio_eventloop_init(&loop);
	if (err != cio_success) {
		return EXIT_FAILURE;
	}

	struct cio_server_socket ss;
	cio_server_socket_init(&ss, &loop, NULL);
	ss.init(&ss, 5);
	ss.set_reuse_address(&ss, true);
	ss.bind(&ss, NULL, 12345);
	ss.accept(&ss, handle_accept, NULL);

	err = cio_eventloop_run(&loop);
	cio_eventloop_destroy(&loop);
	return EXIT_SUCCESS;
}
