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

#include <stddef.h>
#include <stdint.h>

#include "cio_compiler.h"
#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_http_server.h"
#include "cio_read_buffer.h"
#include "cio_server_socket.h"
#include "cio_socket.h"
#include "cio_util.h"

#undef CRLF
#define CRLF "\r\n"

static void handle_request_line(struct cio_buffered_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer)
{
	if (unlikely(err != cio_success)) {
		stream->close(stream);
		return;
	}

	(void)handler_context;
	(void)read_buffer;

}

static void handle_accept(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	if (unlikely(err != cio_success)) {
		ss->close(ss);
		return;
	}

	struct cio_http_client *client = container_of(socket, struct cio_http_client, socket);
	struct cio_http_server *server = (struct cio_http_server *)handler_context;
	client->server = server;

	struct cio_io_stream *stream = socket->get_io_stream(socket);

	cio_read_buffer_init(&client->rb, client->buffer, client->buffer_size);
	cio_buffered_stream_init(&client->bs, stream);
	client->bs.read_until(&client->bs, &client->rb, CRLF, handle_request_line, client);
}

enum cio_error cio_http_server_serve(struct cio_http_server *server)
{

	enum cio_error err = cio_server_socket_init(&server->server_socket, server->loop, 5, server->alloc_client, server->free_client, NULL);
	if (unlikely(err != cio_success)) {
		return err;
	}

	err = server->server_socket.set_reuse_address(&server->server_socket, true);
	if (err != cio_success) {
		goto close_socket;
	}

	err = server->server_socket.bind(&server->server_socket, NULL, 12345);
	if (err != cio_success) {
		goto close_socket;
	}

	err = server->server_socket.accept(&server->server_socket, handle_accept, server);
	if (err != cio_success) {
		goto close_socket;
	}

close_socket:
	server->server_socket.close(&server->server_socket);
	return err;
}
