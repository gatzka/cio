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
#include "cio_http_location.h"
#include "cio_http_location_handler.h"
#include "cio_http_server.h"
#include "cio_read_buffer.h"
#include "cio_server_socket.h"
#include "cio_socket.h"
#include "cio_timer.h"
#include "cio_util.h"
#include "cio_version.h"

#define CIO_CRLF "\r\n"

#define CIO_HTTP_VERSION "HTTP/1.1"

static void handle_error(struct cio_http_server *server, const char *reason)
{
	if (server->on_error != NULL) {
		server->on_error(server, reason);
	}
}

static void close_bs(struct cio_http_client *client)
{

	enum cio_error err = client->bs.close(&client->bs);
	if (unlikely(err != CIO_SUCCESS)) {
		struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
		handle_error(server, "closing buffered stream of client failed");
	}
}

static void close_client(struct cio_http_client *client)
{
	if (likely(client->handler != NULL)) {
		client->handler->free(client->handler);
	}

	client->http_private.read_timer.close(&client->http_private.read_timer);
	close_bs(client);
}

static void client_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;

	if (err == CIO_SUCCESS) {
		struct cio_http_client *client = handler_context;
		close_client(client);
	}
}

static void mark_to_be_closed(struct cio_http_client *client)
{
	if (client->http_private.parsing == 0) {
		close_client(client);
	} else {
		client->http_private.to_be_closed = true;
	}
}

static void response_written(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	(void)bs;
	(void)err;

	struct cio_http_client *client = (struct cio_http_client *)handler_context;
	client->close(client);
}

static const char *get_response_statusline(enum cio_http_status_code status_code)
{
	switch (status_code) {
	case CIO_HTTP_SWITCHING_PROTOCOLS:
		return CIO_HTTP_VERSION " 101 Switching Protocols" CIO_CRLF "Server: cio http/" CIO_VERSION CIO_CRLF;
	case CIO_HTTP_STATUS_OK:
		return CIO_HTTP_VERSION " 200 OK" CIO_CRLF "Server: cio http/" CIO_VERSION CIO_CRLF;
	case CIO_HTTP_STATUS_BAD_REQUEST:
		return CIO_HTTP_VERSION " 400 Bad Request" CIO_CRLF "Server: cio http/" CIO_VERSION CIO_CRLF;
	case CIO_HTTP_STATUS_NOT_FOUND:
		return CIO_HTTP_VERSION " 404 Not Found" CIO_CRLF "Server: cio http/" CIO_VERSION CIO_CRLF;

	default:
		return CIO_HTTP_VERSION " 500 Internal Server Error" CIO_CRLF "Server: cio http/" CIO_VERSION CIO_CRLF;
	}
}

static void queue_header(struct cio_http_client *client, enum cio_http_status_code status_code)
{
	const char *response = get_response_statusline(status_code);
	cio_write_buffer_head_init(&client->wbh);
	cio_write_buffer_const_element_init(&client->http_private.wb_http_response_statusline, response, strlen(response));
	cio_write_buffer_queue_tail(&client->wbh, &client->http_private.wb_http_response_statusline);
	cio_write_buffer_const_element_init(&client->http_private.wb_http_response_header_end, CIO_CRLF, strlen(CIO_CRLF));
	cio_write_buffer_queue_tail(&client->wbh, &client->http_private.wb_http_response_header_end);
}

static void flush(struct cio_http_client *client, cio_buffered_stream_write_handler handler)
{
	enum cio_error err = client->bs.write(&client->bs, &client->wbh, handler, client);
	if (unlikely(err != CIO_SUCCESS)) {
		struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
		handle_error(server, "flushing client responses failed");
		client->close(client);
	}
}

static void write_header(struct cio_http_client *client, enum cio_http_status_code status_code)
{
	queue_header(client, status_code);
	flush(client, response_written);
}

static void write_response(struct cio_http_client *client, struct cio_write_buffer *wbh)
{
	queue_header(client, CIO_HTTP_STATUS_OK);
	cio_write_buffer_splice(wbh, &client->wbh);
	flush(client, response_written);
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

static const struct cio_http_location *find_handler(const struct cio_http_server *server, const char *request_target, size_t url_length)
{
	const struct cio_http_location *best_match = NULL;
	size_t best_match_length = 0;

	const struct cio_http_location *handler = server->first_handler;
	for (size_t i = 0; i < server->num_handlers; i++) {
		size_t location_length = strlen(handler->path);
		if ((location_match(handler->path, location_length, request_target, url_length)) && (location_length > best_match_length)) {
			best_match_length = location_length;
			best_match = handler;
		}

		handler = handler->next;
	}

	return best_match;
}

static int on_headers_complete(http_parser *parser)
{
	struct cio_http_client *client = container_of(parser, struct cio_http_client, parser);
	client->http_private.headers_complete = true;
	client->content_length = parser->content_length;
	if (parser->upgrade) {
		enum cio_error err = client->http_private.read_timer.cancel(&client->http_private.read_timer);
		if (unlikely(err != CIO_SUCCESS)) {
			struct cio_http_server *server = (struct cio_http_server *)parser->data;
			handle_error(server, "Cancelling read timer in on_headers_complete failed, maybe not armed?");
			client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
			return CIO_HTTP_CB_ERROR;
		}

		if (unlikely(client->handler->on_headers_complete == NULL)) {
			client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
			return 0;
		}
	}

	if (client->handler->on_headers_complete != NULL) {
		return client->handler->on_headers_complete(client);
	}

	return 0;
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
	if (parser->upgrade == 0) {
		// In case of an upgraded connection, the read timeout timer was
		// already cancelled in on_headers_complete.
		enum cio_error err = client->http_private.read_timer.cancel(&client->http_private.read_timer);
		if (unlikely(err != CIO_SUCCESS)) {
			struct cio_http_server *server = (struct cio_http_server *)parser->data;
			handle_error(server, "Cancelling read timer in on_message_complete failed, maybe not armed?");
			client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
			return CIO_HTTP_CB_ERROR;
		}
	}

	return client->handler->on_message_complete(client);
}

static int on_body(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = container_of(parser, struct cio_http_client, parser);
	return client->handler->on_body(client, at, length);
}

static enum cio_http_cb_return call_url_parts_callback(const struct http_parser_url *u, enum http_parser_url_fields url_field, cio_http_data_cb callback, struct cio_http_client *client, const char *at)
{
	if (((u->field_set & (1 << url_field)) == (1 << url_field)) && (callback != NULL)) {
		return callback(client, at + u->field_data[url_field].off, u->field_data[url_field].len);
	}

	return CIO_HTTP_CB_UNHANDLED;
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

	const struct cio_http_location *target = find_handler(parser->data, at + u.field_data[UF_PATH].off, u.field_data[UF_PATH].len);
	if (unlikely(target == NULL)) {
		client->write_header(client, CIO_HTTP_STATUS_NOT_FOUND);
		return 0;
	}

	struct cio_http_location_handler *handler = target->alloc_handler(target->config);
	if (unlikely(handler == NULL)) {
		client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
		return 0;
	}

	client->handler = handler;
	handler->client = client;

	int user_handler = 0;

	enum cio_http_cb_return cb_ret = call_url_parts_callback(&u, UF_SCHEMA, handler->on_schema, client, at);
	if (unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	if (cb_ret != CIO_HTTP_CB_UNHANDLED) {
		user_handler = 1;
	}

	cb_ret = call_url_parts_callback(&u, UF_HOST, handler->on_host, client, at);
	if (unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	if (cb_ret != CIO_HTTP_CB_UNHANDLED) {
		user_handler = 1;
	}

	cb_ret = call_url_parts_callback(&u, UF_PORT, handler->on_port, client, at);
	if (unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	if (cb_ret != CIO_HTTP_CB_UNHANDLED) {
		user_handler = 1;
	}

	cb_ret = call_url_parts_callback(&u, UF_PATH, handler->on_path, client, at);
	if (unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	if (cb_ret != CIO_HTTP_CB_UNHANDLED) {
		user_handler = 1;
	}

	cb_ret = call_url_parts_callback(&u, UF_QUERY, handler->on_query, client, at);
	if (unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	if (cb_ret != CIO_HTTP_CB_UNHANDLED) {
		user_handler = 1;
	}

	cb_ret = call_url_parts_callback(&u, UF_FRAGMENT, handler->on_fragment, client, at);
	if (unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	if (cb_ret != CIO_HTTP_CB_UNHANDLED) {
		user_handler = 1;
	}

	client->parser_settings.on_headers_complete = on_headers_complete;

	if (handler->on_header_field != NULL) {
		client->parser_settings.on_header_field = on_header_field;
		user_handler = 1;
	}

	if (handler->on_header_value != NULL) {
		client->parser_settings.on_header_value = on_header_value;
		user_handler = 1;
	}

	if (handler->on_body != NULL) {
		client->parser_settings.on_body = on_body;
		user_handler = 1;
	}

	if (handler->on_message_complete != NULL) {
		client->parser_settings.on_message_complete = on_message_complete;
		user_handler = 1;
	}

	if (handler->on_url != NULL) {
		return handler->on_url(client, at, length);
	}

	if (user_handler == 0) {
		client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
	}

	return 0;
}

static inline bool cio_is_error(enum cio_error error)
{
	return error < CIO_SUCCESS;
}

static void parse(struct cio_buffered_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer)
{
	(void)stream;

	struct cio_http_client *client = (struct cio_http_client *)handler_context;

	if (unlikely(cio_is_error(err))) {
		client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
		return;
	}

	size_t bytes_transfered = cio_read_buffer_get_transferred_bytes(read_buffer);
	client->http_private.parsing++;

	http_parser *parser = &client->parser;
	size_t nparsed = http_parser_execute(parser, &client->parser_settings, (const char *)cio_read_buffer_get_read_ptr(read_buffer), bytes_transfered);
	client->http_private.parsing--;

	if (unlikely(nparsed != bytes_transfered)) {
		client->write_header(client, CIO_HTTP_STATUS_BAD_REQUEST);
		return;
	}

	if (client->http_private.to_be_closed) {
		close_client(client);
		return;
	}

	if (parser->upgrade) {
		return;
	}

	if (bytes_transfered > 0) {
		client->http_private.finish_func(client);
	}
}

static void finish_bytes(struct cio_http_client *client)
{
	enum cio_error err = client->bs.read(&client->bs, &client->rb, parse, client);
	if (unlikely(err != CIO_SUCCESS)) {
		struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
		handle_error(server, "Reading of bytes failed");
		client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
	}
}

static void finish_header_line(struct cio_http_client *client)
{
	enum cio_error err;

	if (!client->http_private.headers_complete) {
		err = client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
	} else {
		client->http_private.finish_func = finish_bytes;
		err = client->bs.read(&client->bs, &client->rb, parse, client);
	}

	if (unlikely(err != CIO_SUCCESS)) {
		struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
		handle_error(server, "Reading of bytes/header line failed");
		client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
	}
}

static void finish_request_line(struct cio_http_client *client)
{
	client->http_major = client->parser.http_major;
	client->http_minor = client->parser.http_minor;
	client->http_private.finish_func = finish_header_line;
	enum cio_error err = client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
	if (unlikely(err != CIO_SUCCESS)) {
		struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
		handle_error(server, "Reading of header line failed");
		client->write_header(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR);
	}
}

static void handle_accept(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)ss;

	struct cio_http_server *server = (struct cio_http_server *)handler_context;

	if (unlikely(err != CIO_SUCCESS)) {
		handle_error(server, "accept failed");
		return;
	}

	struct cio_http_client *client = container_of(socket, struct cio_http_client, socket);

	client->http_private.headers_complete = false;
	client->content_length = 0;
	client->http_private.to_be_closed = false;
	client->http_private.parsing = 0;
	client->close = mark_to_be_closed;
	client->write_header = write_header;
	client->queue_header = queue_header;
	client->write_response = write_response;
	client->flush = flush;

	client->handler = NULL;
	http_parser_settings_init(&client->parser_settings);
	client->parser_settings.on_url = on_url;
	client->parser.data = server;
	http_parser_init(&client->parser, HTTP_REQUEST);

	err = cio_read_buffer_init(&client->rb, client->buffer, client->buffer_size);
	if (unlikely(err != CIO_SUCCESS)) {
		handle_error(server, "read buffer init failed");
		server->free_client(socket);
		return;
	}

	err = cio_buffered_stream_init(&client->bs, socket->get_io_stream(socket));
	if (unlikely(err != CIO_SUCCESS)) {
		handle_error(server, "buffered stream init failed");
		server->free_client(socket);
		return;
	}

	err = cio_timer_init(&client->http_private.read_timer, server->loop, NULL);
	if (unlikely(err != CIO_SUCCESS)) {
		goto init_err;
	}

	err = client->http_private.read_timer.expires_from_now(&client->http_private.read_timer, server->read_timeout_ns, client_timeout_handler, client);
	if (unlikely(err != CIO_SUCCESS)) {
		goto init_err;
	}

	client->http_private.finish_func = finish_request_line;
	err = client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
	if (unlikely(err != CIO_SUCCESS)) {
		goto init_err;
	}

	return;

init_err:
	handle_error(server, "client initialization failed");

	close_bs(client);
}

static enum cio_error serve(struct cio_http_server *server)
{
	enum cio_error err = cio_server_socket_init(&server->server_socket, server->loop, 5, server->alloc_client, server->free_client, NULL);
	if (unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	err = server->server_socket.set_reuse_address(&server->server_socket, true);
	if (unlikely(err != CIO_SUCCESS)) {
		goto close_socket;
	}

	err = server->server_socket.bind(&server->server_socket, NULL, server->port);
	if (unlikely(err != CIO_SUCCESS)) {
		goto close_socket;
	}

	err = server->server_socket.accept(&server->server_socket, handle_accept, server);
	if (unlikely(err != CIO_SUCCESS)) {
		goto close_socket;
	}

	return err;

close_socket:
	server->server_socket.close(&server->server_socket);
	return err;
}

static enum cio_error register_handler(struct cio_http_server *server, struct cio_http_location *target)
{
	if (unlikely(server == NULL) || (target == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	target->next = server->first_handler;
	server->first_handler = target;
	server->num_handlers++;
	return CIO_SUCCESS;
}

enum cio_error cio_http_server_init(struct cio_http_server *server,
                                    uint16_t port,
                                    struct cio_eventloop *loop,
                                    cio_http_serve_on_error on_error,
                                    uint64_t read_timeout_ns,
                                    cio_alloc_client alloc_client,
                                    cio_free_client free_client)
{
	if (unlikely((server == NULL) || (loop == NULL) || (alloc_client == NULL) || (free_client == NULL) || (read_timeout_ns == 0))) {
		return CIO_INVALID_ARGUMENT;
	}

	server->loop = loop;
	server->port = port;
	server->alloc_client = alloc_client;
	server->free_client = free_client;
	server->serve = serve;
	server->register_location = register_handler;
	server->first_handler = NULL;
	server->num_handlers = 0;
	server->on_error = on_error;
	server->read_timeout_ns = read_timeout_ns;
	return CIO_SUCCESS;
}
