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
#include "cio_timer.h"
#include "cio_util.h"

#undef CIO_CRLF
#define CIO_CRLF "\r\n"

#undef CIO_HTTP_VERSION
#define CIO_HTTP_VERSION "HTTP/1.0"

static void close_client(struct cio_http_client *client)
{
	if (likely(client->handler != NULL)) {
		client->handler->free(client->handler);
	}

	client->read_timer.close(&client->read_timer);
	client->bs.close(&client->bs);
}

static void client_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;

	if (err == cio_success) {
		struct cio_http_client *client = handler_context;
		close_client(client);
	}
}

static void mark_to_be_closed(struct cio_http_client *client)
{
	if (client->parsing == 0) {
		close_client(client);
	} else {
		client->to_be_closed = true;
	}
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
		return CIO_HTTP_VERSION " 200 OK" CIO_CRLF CIO_CRLF;
	case cio_http_status_bad_request:
		return CIO_HTTP_VERSION " 400 Bad Request" CIO_CRLF CIO_CRLF;
	case cio_http_status_not_found:
		return CIO_HTTP_VERSION " 404 Not Found" CIO_CRLF CIO_CRLF;

	case cio_http_status_internal_server_error:
	default:
		return CIO_HTTP_VERSION " 500 Internal Server Error" CIO_CRLF CIO_CRLF;
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

static bool location_match(const char *location, size_t location_length, const char *request_target, size_t request_target_length)
{
	if (request_target_length < location_length) {
		return false;
	}

	if (memcmp(request_target, location, location_length) == 0) {
		if (location_length == request_target_length) {
			return true;
		}

		if (location[location_length - 1] == '/') {
			return true;
		}

		if (request_target[location_length] == '/') {
			return true;
		}
	}

	return false;
}

static const struct cio_http_uri_server_location *find_handler(const struct cio_http_server *server, const char *request_target, size_t url_length)
{
	const struct cio_http_uri_server_location *best_match = NULL;
	size_t best_match_length = 0;

	const struct cio_http_uri_server_location *handler = server->first_handler;
	for (size_t i = 0; i < server->num_handlers; i++) {
		size_t location_length = strlen(handler->path);
		if (location_match(handler->path, location_length, request_target, url_length)) {
			if (location_length > best_match_length) {
				best_match_length = location_length;
				best_match = handler;
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
	if (parser->upgrade) {
		client->read_timer.cancel(&client->read_timer);
		if (unlikely(client->handler->on_headers_complete == NULL)) {
			client->write_header(client, cio_http_status_internal_server_error);
			return 0;
		}
	}

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

static int on_message_complete(http_parser *parser)
{
	struct cio_http_client *client = container_of(parser, struct cio_http_client, parser);
	client->read_timer.cancel(&client->read_timer);
	if (client->handler->on_message_complete != NULL) {
		return client->handler->on_message_complete(client);
	} else {
		return 0;
	}
}

static int on_body(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = container_of(parser, struct cio_http_client, parser);
	return client->handler->on_body(client, at, length);
}

static void call_url_parts_callback(const struct http_parser_url *u, enum http_parser_url_fields url_field, cio_http_data_cb callback, struct cio_http_client *client, const char *at)
{
	if (((u->field_set & (1 << url_field)) == (1 << url_field)) && (callback != NULL)) {
		callback(client, at + u->field_data[url_field].off, u->field_data[url_field].len);
	}
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
		return -1;
	}

	const struct cio_http_uri_server_location *target = find_handler(client->server, at + u.field_data[UF_PATH].off, u.field_data[UF_PATH].len);
	if (unlikely(target == NULL)) {
		client->write_header(client, cio_http_status_not_found);
		return 0;
	}

	struct cio_http_request_handler *handler = target->alloc_handler(target->config);
	if (unlikely(handler == NULL)) {
		client->write_header(client, cio_http_status_internal_server_error);
		return 0;
	}

	client->handler = handler;

	if ((handler->on_url == NULL) &&
	    (handler->on_host == NULL) &&
	    (handler->on_path == NULL) &&
	    (handler->on_query == NULL) &&
	    (handler->on_fragment == NULL) &&
	    (handler->on_header_field == NULL) &&
	    (handler->on_header_value == NULL) &&
	    (handler->on_body == NULL) &&
	    (handler->on_message_complete == NULL) &&
	    (handler->on_headers_complete == NULL)) {
		client->write_header(client, cio_http_status_internal_server_error);
		return 0;
	}

	call_url_parts_callback(&u, UF_SCHEMA, handler->on_schema, client, at);
	call_url_parts_callback(&u, UF_HOST, handler->on_host, client, at);
	call_url_parts_callback(&u, UF_PATH, handler->on_path, client, at);
	call_url_parts_callback(&u, UF_QUERY, handler->on_query, client, at);
	call_url_parts_callback(&u, UF_FRAGMENT, handler->on_fragment, client, at);

	client->parser_settings.on_headers_complete = on_headers_complete;

	if (handler->on_header_field != NULL) {
		client->parser_settings.on_header_field = on_header_field;
	}

	if (handler->on_header_value != NULL) {
		client->parser_settings.on_header_value = on_header_value;
	}

	if (handler->on_body != NULL) {
		client->parser_settings.on_body = on_body;
	}

	if (handler->on_message_complete != NULL) {
		client->parser_settings.on_message_complete = on_message_complete;
	}

	if (handler->on_url != NULL) {
		return handler->on_url(client, at, length);
	}

	return 0;
}

static void parse(struct cio_buffered_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer)
{
	(void)stream;

	struct cio_http_client *client = (struct cio_http_client *)handler_context;

	if (unlikely(cio_is_error(err))) {
		client->write_header(client, cio_http_status_internal_server_error);
		return;
	}

	size_t bytes_transfered = cio_read_buffer_get_transferred_bytes(read_buffer);
	client->parsing++;
	size_t nparsed = http_parser_execute(&client->parser, &client->parser_settings, (const char *)cio_read_buffer_get_read_ptr(read_buffer), bytes_transfered);
	client->parsing--;

	if (unlikely(nparsed != bytes_transfered)) {
		client->write_header(client, cio_http_status_bad_request);
	}

	if (client->to_be_closed) {
		close_client(client);
		return;
	}

	if (bytes_transfered > 0) {
		client->finish_func(client);
	}
}

static void finish_bytes(struct cio_http_client *client)
{
	client->bs.read(&client->bs, &client->rb, parse, client);
}

static void finish_header_line(struct cio_http_client *client)
{
	if (!client->headers_complete) {
		client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
	} else {
		client->finish_func = finish_bytes;
		client->bs.read(&client->bs, &client->rb, parse, client);
	}
}

static void finish_request_line(struct cio_http_client *client)
{
	client->http_major = client->parser.http_major;
	client->http_minor = client->parser.http_minor;
	client->finish_func = finish_header_line;
	client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
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

	client->headers_complete = false;
	client->content_length = 0;
	client->to_be_closed = false;
	client->parsing = 0;
	client->close = mark_to_be_closed;
	client->write_header = write_header;
	client->write_response = write_response;

	client->handler = NULL;
	http_parser_settings_init(&client->parser_settings);
	client->parser_settings.on_url = on_url;
	http_parser_init(&client->parser, HTTP_REQUEST);

	cio_read_buffer_init(&client->rb, client->buffer, client->buffer_size);
	cio_buffered_stream_init(&client->bs, socket->get_io_stream(socket));

	err = cio_timer_init(&client->read_timer, server->loop, NULL);
	if (unlikely(err != cio_success)) {
		if (server->error_cb != NULL) {
			server->error_cb(server);
		}

		client->bs.close(&client->bs);
		return;
	}

	client->read_timer.expires_from_now(&client->read_timer, server->read_timeout, client_timeout_handler, client);
	client->finish_func = finish_request_line;
	client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
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

static enum cio_error register_handler(struct cio_http_server *server, struct cio_http_uri_server_location *target)
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
                                    uint64_t read_timeout,
                                    cio_alloc_client alloc_client,
                                    cio_free_client free_client)
{
	if (unlikely((server == NULL) || (loop == NULL) || (alloc_client == NULL) || (free_client == NULL) || (read_timeout == 0))) {
		return cio_invalid_argument;
	}

	server->loop = loop;
	server->port = port;
	server->alloc_client = alloc_client;
	server->free_client = free_client;
	server->serve = serve;
	server->register_location = register_handler;
	server->first_handler = NULL;
	server->num_handlers = 0;
	server->error_cb = error_cb;
	server->read_timeout = read_timeout;
	return cio_success;
}

enum cio_error cio_http_server_location_init(struct cio_http_uri_server_location *location, const char *path, const void *config, cio_http_alloc_handler handler)
{
	if (unlikely((location == NULL) || (path == NULL) || (handler == NULL))) {
		return cio_invalid_argument;
	}

	location->config = config;
	location->next = NULL;
	location->alloc_handler = handler;
	location->path = path;

	return cio_success;
}

void cio_http_request_handler_init(struct cio_http_request_handler *handler)
{
	handler->on_url = NULL;
	handler->on_schema = NULL;
	handler->on_host = NULL;
	handler->on_path = NULL;
	handler->on_query = NULL;
	handler->on_fragment = NULL;
	handler->on_header_field = NULL;
	handler->on_header_value = NULL;
	handler->on_headers_complete = NULL;
	handler->on_body = NULL;
	handler->on_message_complete = NULL;
	handler->free = NULL;
}
