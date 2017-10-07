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
#include <string.h>

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


static void response_written(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	(void)handler_context;
	(void)buffer;
	(void)err;
	bs->close(bs);
}

static const char *get_response(unsigned int status_code)
{
	switch (status_code) {
	case HTTP_BAD_REQUEST:
		return "HTTP/1.0 400 Bad Request" CRLF CRLF;
	case HTTP_NOT_FOUND:
		return "HTTP/1.0 404 Not Found" CRLF CRLF;

	case HTTP_INTERNAL_SERVER_ERROR:
	default:
		return "HTTP/1.0 500 Internal Server Error" CRLF CRLF;
	}
}

static void send_http_error_response(struct cio_http_client *client, unsigned int status_code)
{
	const char *response = get_response(status_code);
	cio_write_buffer_head_init(&client->wbh);
	cio_write_buffer_init(&client->wb, response, strlen(response));
	cio_write_buffer_queue_tail(&client->wbh, &client->wb);
	client->bs.write(&client->bs, &client->wbh, response_written, client);
}

static void handle_request_line(struct cio_buffered_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer)
{
	(void)stream;
	struct cio_http_client *client = (struct cio_http_client *)handler_context;

	if (unlikely(err != cio_success)) {
		send_http_error_response(client, HTTP_INTERNAL_SERVER_ERROR);
		return;
	}

	size_t bytes_transfered = cio_read_buffer_get_transferred_bytes(read_buffer);
	size_t nparsed = http_parser_execute(&client->parser, &client->parser_settings, (const char *)cio_read_buffer_get_read_ptr(read_buffer), bytes_transfered);

	if (unlikely(nparsed != bytes_transfered)) {
		send_http_error_response(client, HTTP_BAD_REQUEST);
		return;
	}

#if 0
1.	struct cio_request_target_hander handler = suche;
	client->parser_settings.on_header_field = handler.on_header_field;
	client->parser_settings.on_header_value = handler.on_header_value;
#endif
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

	http_parser_settings_init(&client->parser_settings);
	http_parser_init(&client->parser, HTTP_REQUEST);

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

	err = server->server_socket.bind(&server->server_socket, NULL, server->port);
	if (err != cio_success) {
		goto close_socket;
	}

	err = server->server_socket.accept(&server->server_socket, handle_accept, server);
	if (err != cio_success) {
		goto close_socket;
	}

	return err;

close_socket:
	server->server_socket.close(&server->server_socket);
	return err;
}
