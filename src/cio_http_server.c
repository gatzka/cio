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

#include "cio_buffered_stream.h"
#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_http_server.h"
#include "cio_read_buffer.h"
#include "cio_server_socket.h"
#include "cio_socket.h"
#include "cio_util.h"

#undef CRLF
#define CRLF "\r\n"

static void close_client(struct cio_http_client *client)
{
	if (likely(client->handler != NULL)) {
		client->handler->free(client->handler);
	}

	client->bs.close(&client->bs);
}

static void mark_to_be_closed(struct cio_http_client *client)
{
	client->to_be_closed = true;
}

static void response_written(struct cio_buffered_stream *bs, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	(void)bs;
	(void)buffer;
	(void)err;

	struct cio_http_client *client = (struct cio_http_client *)handler_context;
	client->close(client);
}

static const char *get_response(enum cio_http_status_code status_code)
{
	switch (status_code) {
	case cio_http_status_ok:
		return "HTTP/1.0 200 OK" CRLF CRLF;
	case cio_http_status_bad_request:
		return "HTTP/1.0 400 Bad Request" CRLF CRLF;
	case cio_http_status_not_found:
		return "HTTP/1.0 404 Not Found" CRLF CRLF;

	case cio_http_status_internal_server_error:
	default:
		return "HTTP/1.0 500 Internal Server Error" CRLF CRLF;
	}
}

static void queue_header(struct cio_http_client *client, enum cio_http_status_code status_code)
{
	const char *response = get_response(status_code);
	cio_write_buffer_head_init(&client->wbh);
	cio_write_buffer_init(&client->wb_http_header, response, strlen(response));
	cio_write_buffer_queue_tail(&client->wbh, &client->wb_http_header);
}

static void write_header(struct cio_http_client *client, enum cio_http_status_code status_code)
{
	queue_header(client, status_code);
	client->bs.write(&client->bs, &client->wbh, response_written, client);
}

static void write_response(struct cio_http_client *client, struct cio_write_buffer *wbh)
{
	queue_header(client, cio_http_status_ok);
	cio_write_buffer_splice(wbh, &client->wbh);
	client->bs.write(&client->bs, &client->wbh, response_written, client);
}

static const struct cio_http_request_target *find_handler(const struct cio_http_server *server, const char *request_target, size_t url_length)
{
	const struct cio_http_request_target *best_match = NULL;
	size_t best_match_length = 0;

	const struct cio_http_request_target *handler = server->first_handler;
	for (size_t i = 0; i < server->num_handlers; i++) {
		size_t length = strlen(handler->request_target);
		if (length <= url_length) {
			if (memcmp(handler->request_target, request_target, length) == 0) {
				if (length > best_match_length) {
					best_match_length = length;
					best_match = handler;
				}
			}
		}

		handler = handler->next;
	}

	return best_match;
}

static int on_headers_complete(http_parser *parser)
{
	struct cio_http_client *client = container_of(parser, struct cio_http_client, parser);
	client->headers_complete = true;
	client->content_length = parser->content_length;
	if (client->handler->on_headers_complete != NULL) {
		return client->handler->on_headers_complete(client);
	} else {
		return 0;
	}
}

static int on_header_field(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = container_of(parser, struct cio_http_client, parser);
	return client->handler->on_header_field(client, at, length);
}

static int on_header_value(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = container_of(parser, struct cio_http_client, parser);
	return client->handler->on_header_value(client, at, length);
}

static int on_url(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = container_of(parser, struct cio_http_client, parser);

	int is_connect;
	if (unlikely(parser->method == HTTP_CONNECT)) {
		is_connect = 1;
	} else {
		is_connect = 0;
	}

	client->http_method = client->parser.method;

	struct http_parser_url u;
	http_parser_url_init(&u);
	int ret = http_parser_parse_url(at, length, is_connect, &u);
	if ((unlikely(ret != 0)) || !((u.field_set & (1 << UF_PATH)) == (1 << UF_PATH))) {
		client->write_header(client, cio_http_status_bad_request);
		return -1;
	} else {
		const struct cio_http_request_target *target = find_handler(client->server, at + u.field_data[UF_PATH].off, u.field_data[UF_PATH].len);
		if (unlikely(target == NULL)) {
			client->write_header(client, cio_http_status_not_found);
			return 0;
		} else {
			struct cio_http_request_handler *handler = target->alloc_handler(target->config);
			if (unlikely(handler == NULL)) {
				client->write_header(client, cio_http_status_internal_server_error);
				return -1;
			}

			client->handler = handler;

			client->parser_settings.on_headers_complete = on_headers_complete;

			if (handler->on_header_field != NULL) {
				client->parser_settings.on_header_field = on_header_field;
			}

			if (handler->on_header_value != NULL) {
				client->parser_settings.on_header_value = on_header_value;
			}

			if (handler->on_url != NULL) {
				return handler->on_url(client, at, length);
			}

			return 0;
		}
	}
}

static void handle_line(struct cio_buffered_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer)
{
	(void)stream;
	struct cio_http_client *client = (struct cio_http_client *)handler_context;

	if (unlikely(err != cio_success)) {
		client->write_header(client, cio_http_status_internal_server_error);
		return;
	}

	size_t bytes_transfered = cio_read_buffer_get_transferred_bytes(read_buffer);
	const char *read_ptr = (const char *)cio_read_buffer_get_read_ptr(read_buffer);
	size_t nparsed = http_parser_execute(&client->parser, &client->parser_settings, read_ptr, bytes_transfered);

	if (unlikely(nparsed != bytes_transfered)) {
		client->write_header(client, cio_http_status_bad_request);
		return;
	}

	if (!client->headers_complete) {
		client->bs.read_until(&client->bs, &client->rb, CRLF, handle_line, client);
	}

	if (client->to_be_closed) {
		close_client(client);
	}
}

static void handle_request_line(struct cio_buffered_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer)
{
	(void)stream;
	struct cio_http_client *client = (struct cio_http_client *)handler_context;

	if (unlikely(err != cio_success)) {
		client->write_header(client, cio_http_status_internal_server_error);
		close_client(client);
		return;
	}

	size_t bytes_transfered = cio_read_buffer_get_transferred_bytes(read_buffer);
	size_t nparsed = http_parser_execute(&client->parser, &client->parser_settings, (const char *)cio_read_buffer_get_read_ptr(read_buffer), bytes_transfered);

	if (unlikely(nparsed != bytes_transfered)) {
		client->write_header(client, cio_http_status_bad_request);
		close_client(client);
		return;
	}

	client->http_major = client->parser.http_major;
	client->http_minor = client->parser.http_minor;

	client->bs.read_until(&client->bs, &client->rb, CRLF, handle_line, client);
}

static void handle_accept(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)ss;

	struct cio_http_server *server = (struct cio_http_server *)handler_context;

	if (unlikely(err != cio_success)) {
		if (server->error_cb != NULL) {
			server->error_cb(server);
		}
		return;
	}

	struct cio_http_client *client = container_of(socket, struct cio_http_client, socket);
	client->server = server;

	struct cio_io_stream *stream = socket->get_io_stream(socket);

	client->headers_complete = false;
	client->content_length = 0;
	client->to_be_closed = false;
	client->close = mark_to_be_closed;
	client->write_header = write_header;
	client->write_response = write_response;

	client->handler = NULL;
	http_parser_settings_init(&client->parser_settings);
	client->parser_settings.on_url = on_url;
	http_parser_init(&client->parser, HTTP_REQUEST);

	cio_read_buffer_init(&client->rb, client->buffer, client->buffer_size);
	cio_buffered_stream_init(&client->bs, stream);
	client->bs.read_until(&client->bs, &client->rb, CRLF, handle_request_line, client);
}

static enum cio_error serve(struct cio_http_server *server)
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

static enum cio_error register_handler(struct cio_http_server *server, struct cio_http_request_target *target)
{
	if (unlikely(server == NULL) || (target == NULL)) {
		return cio_invalid_argument;
	}

	target->next = server->first_handler;
	server->first_handler = target;
	server->num_handlers++;
	return cio_success;
}

enum cio_error cio_http_server_init(struct cio_http_server *server,
									uint16_t port,
									struct cio_eventloop *loop,
									cio_http_serve_error_cb error_cb,
									cio_alloc_client alloc_client,
									cio_free_client free_client)
{
	if (unlikely((server == NULL) || (loop == NULL) || (alloc_client == NULL) || (free_client == NULL))) {
		return cio_invalid_argument;
	}

	server->loop = loop;
	server->port = port;
	server->alloc_client = alloc_client;
	server->free_client = free_client;
	server->serve = serve;
	server->register_target = register_handler;
	server->first_handler = NULL;
	server->num_handlers = 0;
	server->error_cb = error_cb;
	return cio_success;
}

enum cio_error cio_http_request_target_init(struct cio_http_request_target *target, const char *request_target, const void *config, cio_alloc_handler handler)
{
	if (unlikely((target == NULL) || (request_target == NULL) || (handler == NULL))) {
		return cio_invalid_argument;
	}

	target->config = config;
	target->next = NULL;
	target->alloc_handler = handler;
	target->request_target = request_target;

	return cio_success;
}
