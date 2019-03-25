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

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cio_buffered_stream.h"
#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_http_client.h"
#include "cio_http_location.h"
#include "cio_http_location_handler.h"
#include "cio_http_server.h"
#include "cio_http_status_code.h"
#include "cio_read_buffer.h"
#include "cio_server_socket.h"
#include "cio_socket.h"
#include "cio_timer.h"
#include "cio_util.h"
#include "cio_version_private.h"
#include "cio_write_buffer.h"
#include "http-parser/http_parser.h"

#define CIO_CRLF "\r\n"
#define HTTP_SERVER_ID "Server: cio http/"
#define CIO_HTTP_CONNECTION_CLOSE "Connection: close" CIO_CRLF
#define CIO_HTTP_CONNECTION_KEEPALIVE "Connection: keep-alive" CIO_CRLF
#define CIO_HTTP_CONNECTION_UPGRADE "Connection: Upgrade" CIO_CRLF

#define CIO_HTTP_VERSION "HTTP/1.1"
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

static const uint32_t NANO_SECONDS_IN_SECONDS = 1000000000;

static int on_url(http_parser *parser, const char *at, size_t length);
static void finish_request_line(struct cio_http_client *client);
static enum cio_error write_response(struct cio_http_client *client, enum cio_http_status_code status_code, struct cio_write_buffer *wbh, void (*response_written_cb)(struct cio_http_client *client, enum cio_error err));
static void parse(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t bytes_to_parse);
static void handle_server_error(struct cio_http_client *client, const char *msg);

static void handle_error(struct cio_http_server *server, const char *reason)
{
	if (server->on_error != NULL) {
		server->on_error(server, reason);
	}
}

static void close_bs(struct cio_http_client *client)
{
	enum cio_error err = client->bs.close(&client->bs);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
		handle_error(server, "closing buffered stream of client failed");
		server->free_client(&client->socket);
	}
}

static void free_handler(struct cio_http_client *client)
{
	if (cio_likely(client->current_handler != NULL)) {
		client->current_handler->free(client->current_handler);
		client->current_handler = NULL;
	}
}

static void notify_free_handler_and_close_stream(struct cio_http_client *client)
{
	free_handler(client);
	close_bs(client);
}

static void close_client(struct cio_http_client *client)
{
	client->http_private.request_timer.close(&client->http_private.request_timer);
	client->http_private.response_timer.close(&client->http_private.response_timer);
	notify_free_handler_and_close_stream(client);
}

static void mark_to_be_closed(struct cio_http_client *client)
{
	if (client->http_private.parsing == 0) {
		close_client(client);
	} else {
		client->http_private.to_be_closed = true;
	}
}

static void client_timeout_handler(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)timer;

	if (err == CIO_SUCCESS) {
		struct cio_http_client *client = handler_context;
		err = write_response(client, CIO_HTTP_STATUS_TIMEOUT, NULL, NULL);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			mark_to_be_closed(client);
		}
	}
}

static void restart_read_request(struct cio_http_client *client)
{
	if (client->request_complete) {
		free_handler(client);
		struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
		enum cio_error err = client->http_private.request_timer.expires_from_now(&client->http_private.request_timer, server->read_header_timeout_ns, client_timeout_handler, client);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			handle_server_error(client, "Could not re-arm timer for restarting a read request");
			return;
		}

		cio_write_buffer_head_init(&client->response_wbh);
		http_parser_settings_init(&client->parser_settings);
		client->parser_settings.on_url = on_url;
		http_parser_init(&client->parser, HTTP_REQUEST);

		client->http_private.response_fired = false;
		client->request_complete = false;
		client->response_written = false;

		client->http_private.finish_func = finish_request_line;
		err = client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			handle_server_error(client, "Could not restart read request");
		}
	}
}

static void response_written(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	(void)bs;
	struct cio_http_client *client = (struct cio_http_client *)handler_context;
	enum cio_error cancel_err = client->http_private.response_timer.cancel(&client->http_private.response_timer);

	client->response_written = true;
	if (client->response_written_cb) {
		client->response_written_cb(client, err);
	}

	if (cio_unlikely(cancel_err != CIO_SUCCESS)) {
		handle_server_error(client, "Cancelling response timer failed!");
		return;
	}

	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_server_error(client, "Writing response failed!");
		return;
	}

	if (cio_unlikely(client->http_private.close_immediately || !client->http_private.should_keepalive)) {
		mark_to_be_closed(client);
		return;
	}

	restart_read_request(client);
}

static const char *get_response_statusline(enum cio_http_status_code status_code)
{
	switch (status_code) {
	case CIO_HTTP_STATUS_SWITCHING_PROTOCOLS:
		return CIO_HTTP_VERSION " 101 Switching Protocols" CIO_CRLF HTTP_SERVER_ID CIO_VERSION CIO_CRLF;
	case CIO_HTTP_STATUS_OK:
		return CIO_HTTP_VERSION " 200 OK" CIO_CRLF HTTP_SERVER_ID CIO_VERSION CIO_CRLF;
	case CIO_HTTP_STATUS_BAD_REQUEST:
		return CIO_HTTP_VERSION " 400 Bad Request" CIO_CRLF HTTP_SERVER_ID CIO_VERSION CIO_CRLF;
	case CIO_HTTP_STATUS_NOT_FOUND:
		return CIO_HTTP_VERSION " 404 Not Found" CIO_CRLF HTTP_SERVER_ID CIO_VERSION CIO_CRLF;
	case CIO_HTTP_STATUS_TIMEOUT:
		return CIO_HTTP_VERSION " 408 Request Timeout" CIO_CRLF HTTP_SERVER_ID CIO_VERSION CIO_CRLF;
	default:
		return CIO_HTTP_VERSION " 500 Internal Server Error" CIO_CRLF HTTP_SERVER_ID CIO_VERSION CIO_CRLF;
	}
}

static void add_response_header(struct cio_http_client *client, struct cio_write_buffer *wbh)
{
	cio_write_buffer_queue_tail(&client->response_wbh, wbh);
}

static void start_response_header(struct cio_http_client *client, enum cio_http_status_code status_code)
{
	const char *response = get_response_statusline(status_code);
	cio_write_buffer_const_element_init(&client->http_private.wb_http_response_statusline, response, strlen(response));
	cio_write_buffer_queue_head(&client->response_wbh, &client->http_private.wb_http_response_statusline);

	if (status_code != CIO_HTTP_STATUS_SWITCHING_PROTOCOLS) {
		if (cio_likely(client->http_private.should_keepalive && !client->http_private.close_immediately)) {
			struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
			cio_write_buffer_const_element_init(&client->http_private.wb_http_connection_header, CIO_HTTP_CONNECTION_KEEPALIVE, strlen(CIO_HTTP_CONNECTION_KEEPALIVE));
			cio_write_buffer_const_element_init(&client->http_private.wb_http_keepalive_header, server->keepalive_header, strlen(server->keepalive_header));
			add_response_header(client, &client->http_private.wb_http_keepalive_header);
		} else {
			cio_write_buffer_const_element_init(&client->http_private.wb_http_connection_header, CIO_HTTP_CONNECTION_CLOSE, strlen(CIO_HTTP_CONNECTION_CLOSE));
		}
	} else {
		cio_write_buffer_const_element_init(&client->http_private.wb_http_connection_header, CIO_HTTP_CONNECTION_UPGRADE, strlen(CIO_HTTP_CONNECTION_UPGRADE));
	}

	add_response_header(client, &client->http_private.wb_http_connection_header);
}

static void end_response_header(struct cio_http_client *client)
{
	cio_write_buffer_const_element_init(&client->http_private.wb_http_response_header_end, CIO_CRLF, strlen(CIO_CRLF));
	cio_write_buffer_queue_tail(&client->response_wbh, &client->http_private.wb_http_response_header_end);
}

static enum cio_error flush(struct cio_http_client *client, cio_buffered_stream_write_handler handler)
{
	return client->bs.write(&client->bs, &client->response_wbh, handler, client);
}

static enum cio_error write_response(struct cio_http_client *client, enum cio_http_status_code status_code, struct cio_write_buffer *wbh_body, cio_response_written_cb written_cb)
{
	if (cio_unlikely(client->response_written)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	client->response_written_cb = written_cb;
	client->http_private.response_fired = true;
	size_t content_length;
	if (wbh_body) {
		content_length = cio_write_buffer_get_total_size(wbh_body);
	} else {
		content_length = 0;
	}

	int written = snprintf(client->http_private.content_length_buffer, sizeof(client->http_private.content_length_buffer) - 1, "Content-Length: %zu" CIO_CRLF, content_length);
	cio_write_buffer_element_init(&client->http_private.wb_http_content_length, client->http_private.content_length_buffer, (size_t)written);
	add_response_header(client, &client->http_private.wb_http_content_length);

	if ((status_code == CIO_HTTP_STATUS_BAD_REQUEST) || (status_code == CIO_HTTP_STATUS_TIMEOUT) || (status_code == CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR)) {
		client->http_private.close_immediately = true;
	}

	struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
	enum cio_error err = client->http_private.response_timer.expires_from_now(&client->http_private.response_timer, server->response_timeout_ns, client_timeout_handler, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_error(server, "Arming of response timer failed!");
		mark_to_be_closed(client);
		return 0;
	}

	start_response_header(client, status_code);
	end_response_header(client);
	if (wbh_body) {
		cio_write_buffer_splice(wbh_body, &client->response_wbh);
	}

	return flush(client, response_written);
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

static const struct cio_http_location *find_location(const struct cio_http_server *server, const char *request_target, size_t url_length)
{
	const struct cio_http_location *best_match = NULL;
	size_t best_match_length = 0;

	const struct cio_http_location *location = server->first_location;
	for (size_t i = 0; i < server->num_handlers; i++) {
		size_t location_length = strlen(location->path);
		if ((location_length > 0) && (location_match(location->path, location_length, request_target, url_length)) && (location_length > best_match_length)) {
			best_match_length = location_length;
			best_match = location;
		}

		location = location->next;
	}

	return best_match;
}

static void handle_server_error(struct cio_http_client *client, const char *msg)
{
	struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
	handle_error(server, msg);
	enum cio_error err = write_response(client, CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR, NULL, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		mark_to_be_closed(client);
	}
}

static void finish_bytes(struct cio_http_client *client)
{
	enum cio_error err = client->bs.read_at_least(&client->bs, &client->rb, 1, parse, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_server_error(client, "Reading of bytes failed");
	}
}

static int on_headers_complete(http_parser *parser)
{
	struct cio_http_client *client = cio_container_of(parser, struct cio_http_client, parser);
	client->http_private.headers_complete = true;
	client->http_private.should_keepalive = (http_should_keep_alive(parser) == 1) ? true : false;
	if (cio_unlikely(!client->http_private.should_keepalive && client->http_private.response_fired)) {
		mark_to_be_closed(client);
	}

	if ((parser->content_length > SIZE_MAX) && (parser->content_length != ULLONG_MAX)) {
		handle_server_error(client, "Content-Length is too large to handle!");
		return 0;
	}

	client->content_length = (size_t)parser->content_length;

	enum cio_error err = client->http_private.request_timer.cancel(&client->http_private.request_timer);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_server_error(client, "Cancelling read timer in on_headers_complete failed, maybe not armed?");
		return 0;
	}

	if (!client->http_private.response_fired && client->current_handler->on_headers_complete) {
		int ret = client->current_handler->on_headers_complete(client);
		if (cio_unlikely(ret != CIO_HTTP_CB_SUCCESS)) {
			return ret;
		}
	}

	struct cio_http_server *server = (struct cio_http_server *)client->parser.data;
	err = client->http_private.request_timer.expires_from_now(&client->http_private.request_timer, server->read_body_timeout_ns, client_timeout_handler, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_server_error(client, "Arming of body read timer failed!");
		return 0;
	}

	if (client->content_length > 0) {
		client->http_private.finish_func = finish_bytes;
		client->http_private.remaining_content_length = client->content_length;
	}

	return 0;
}

static int data_callback(struct cio_http_client *client, const char *at, size_t length, cio_http_data_cb cb)
{
	if (!client->http_private.response_fired && cb) {
		return cb(client, at, length);
	}

	return CIO_HTTP_CB_SUCCESS;
}

static int on_header_field(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = cio_container_of(parser, struct cio_http_client, parser);
	return data_callback(client, at, length, client->current_handler->on_header_field);
}

static int on_header_value(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = cio_container_of(parser, struct cio_http_client, parser);
	return data_callback(client, at, length, client->current_handler->on_header_value);
}

static int on_message_complete(http_parser *parser)
{
	struct cio_http_client *client = cio_container_of(parser, struct cio_http_client, parser);

	if (cio_unlikely(!client->parser.upgrade)) {
		enum cio_error err = client->http_private.request_timer.cancel(&client->http_private.request_timer);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			handle_server_error(client, "Cancelling read timer in on_message_complete failed, maybe not armed?");
			return 0;
		}

		client->http_private.finish_func = restart_read_request;
	}

	enum cio_http_cb_return ret;
	if (!client->http_private.response_fired && client->current_handler->on_message_complete) {
		ret = client->current_handler->on_message_complete(client);
	} else {
		ret = CIO_HTTP_CB_SUCCESS;
	}

	if (cio_unlikely(!client->response_written)) {
		handle_server_error(client, "After receiving the complete message, no response was written!");
		return 0;
	}

	client->request_complete = true;
	return ret;
}

static int on_body(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = cio_container_of(parser, struct cio_http_client, parser);
	if (!client->http_private.response_fired && client->current_handler->on_body) {
		return client->current_handler->on_body(client, at, length);
	}

	return 0;
}

static enum cio_http_cb_return call_url_parts_callback(const struct http_parser_url *u, enum http_parser_url_fields url_field, cio_http_data_cb callback, struct cio_http_client *client, const char *at)
{
	if ((u->field_set & (1U << url_field)) == (1U << url_field)) {
		return data_callback(client, at + u->field_data[url_field].off, u->field_data[url_field].len, callback);
	}

	return CIO_HTTP_CB_SUCCESS;
}

static bool path_in_url(const struct http_parser_url *u)
{
	return ((u->field_set & (1U << (unsigned int)UF_PATH)) == (1U << (unsigned int)UF_PATH));
}

static int on_url(http_parser *parser, const char *at, size_t length)
{
	struct cio_http_client *client = cio_container_of(parser, struct cio_http_client, parser);

	client->http_method = (enum cio_http_method)client->parser.method;

	client->parser_settings.on_headers_complete = on_headers_complete;
	client->parser_settings.on_header_field = on_header_field;
	client->parser_settings.on_header_value = on_header_value;
	client->parser_settings.on_body = on_body;
	client->parser_settings.on_message_complete = on_message_complete;

	struct http_parser_url u;
	http_parser_url_init(&u);
	int ret = http_parser_parse_url(at, length, cio_unlikely(parser->method == (unsigned int)HTTP_CONNECT) ? 1 : 0, &u);
	if (cio_unlikely(ret != 0)) {
		return -1;
	}

	if (cio_unlikely(!path_in_url(&u))) {
		return -1;
	}

	const struct cio_http_location *location = find_location(parser->data, at + u.field_data[UF_PATH].off, u.field_data[UF_PATH].len);
	if (cio_unlikely(location == NULL)) {
		write_response(client, CIO_HTTP_STATUS_NOT_FOUND, NULL, NULL);
		return 0;
	}

	struct cio_http_location_handler *handler = location->alloc_handler(location->config);
	if (cio_unlikely(handler == NULL)) {
		handle_server_error(client, "Allocation of handler failed!");
		return 0;
	}

	if (cio_unlikely(handler->free == NULL)) {
		handle_server_error(client, "Handler has no function to free!");
		return 0;
	}

	client->current_handler = handler;

	if (cio_unlikely(cio_http_location_handler_no_callbacks(handler))) {
		handle_server_error(client, "No callbacks for given set in handler!");
		return 0;
	}

	enum cio_http_cb_return cb_ret = call_url_parts_callback(&u, UF_SCHEMA, handler->on_schema, client, at);
	if (cio_unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	cb_ret = call_url_parts_callback(&u, UF_HOST, handler->on_host, client, at);
	if (cio_unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	cb_ret = call_url_parts_callback(&u, UF_PORT, handler->on_port, client, at);
	if (cio_unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	cb_ret = call_url_parts_callback(&u, UF_PATH, handler->on_path, client, at);
	if (cio_unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	cb_ret = call_url_parts_callback(&u, UF_QUERY, handler->on_query, client, at);
	if (cio_unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	cb_ret = call_url_parts_callback(&u, UF_FRAGMENT, handler->on_fragment, client, at);
	if (cio_unlikely(cb_ret == CIO_HTTP_CB_ERROR)) {
		return -1;
	}

	if (handler->on_url && !client->http_private.response_fired) {
		return handler->on_url(client, at, length);
	}

	return 0;
}

static inline bool cio_is_error(enum cio_error error)
{
	return error < CIO_SUCCESS;
}

static void parse(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t bytes_to_parse)
{
	(void)bs;

	struct cio_http_client *client = (struct cio_http_client *)handler_context;
	http_parser *parser = &client->parser;

	if (cio_unlikely(cio_is_error(err))) {
		handle_server_error(client, "Reading from buffered stream failed!");
		return;
	}

	if (err == CIO_EOF) {
		bytes_to_parse = 0;
	}

	if (client->http_private.remaining_content_length > 0) {
		size_t available = cio_read_buffer_unread_bytes(read_buffer);
		bytes_to_parse = MIN(available, client->http_private.remaining_content_length);
		client->http_private.remaining_content_length -= bytes_to_parse;
	}

	client->http_private.parsing++;

	size_t nparsed = http_parser_execute(parser, &client->parser_settings, (const char *)cio_read_buffer_get_read_ptr(read_buffer), bytes_to_parse);
	cio_read_buffer_consume(read_buffer, nparsed);
	client->http_private.parsing--;

	if (err == CIO_EOF) {
		close_client(client);
		return;
	}

	if (cio_unlikely(nparsed != bytes_to_parse)) {
		err = write_response(client, CIO_HTTP_STATUS_BAD_REQUEST, NULL, NULL);
		if (cio_unlikely(err != CIO_SUCCESS)) {
			close_client(client);
		}

		return;
	}

	if (client->http_private.to_be_closed) {
		close_client(client);
		return;
	}

	if (parser->upgrade) {
		return;
	}

	client->http_private.finish_func(client);
}

static void finish_header_line(struct cio_http_client *client)
{
	enum cio_error err = client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_server_error(client, "Reading of header line failed");
	}
}

static void finish_request_line(struct cio_http_client *client)
{
	client->http_major = client->parser.http_major;
	client->http_minor = client->parser.http_minor;
	client->http_private.finish_func = finish_header_line;
	enum cio_error err = client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_server_error(client, "Reading of header line failed");
	}
}

static void handle_accept(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)ss;

	struct cio_http_server *server = (struct cio_http_server *)handler_context;
	struct cio_io_stream *stream = socket->get_io_stream(socket);

	if (cio_unlikely((err != CIO_SUCCESS) || (stream == NULL))) {
		handle_error(server, "accept failed");
		if (stream == NULL) {
			server->free_client(socket);
		} else {
			stream->close(stream);
		}

		return;
	}

	struct cio_http_client *client = cio_container_of(socket, struct cio_http_client, socket);

	client->http_private.headers_complete = false;
	client->content_length = 0;
	client->http_private.to_be_closed = false;
	client->http_private.remaining_content_length = 0;
	client->http_private.should_keepalive = true;
	client->http_private.close_immediately = false;
	client->http_private.parsing = 0;
	client->http_private.response_fired = false;
	client->close = mark_to_be_closed;
	client->add_response_header = add_response_header;
	client->write_response = write_response;
	client->response_written_cb = NULL;
	client->response_written = false;
	client->request_complete = false;

	cio_write_buffer_head_init(&client->response_wbh);

	client->current_handler = NULL;
	http_parser_settings_init(&client->parser_settings);
	client->parser_settings.on_url = on_url;
	client->parser.data = server;
	http_parser_init(&client->parser, HTTP_REQUEST);

	err = cio_read_buffer_init(&client->rb, client->buffer, client->buffer_size);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		handle_error(server, "read buffer init failed");
		stream->close(stream);
		return;
	}

	// Deliberatly no check for return value. bs is part of client and the check for stream != NULL was done before.
	cio_buffered_stream_init(&client->bs, stream);

	err = cio_timer_init(&client->http_private.response_timer, server->loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto response_timer_init_err;
	}

	err = cio_timer_init(&client->http_private.request_timer, server->loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto request_timer_init_err;
	}

	err = client->http_private.request_timer.expires_from_now(&client->http_private.request_timer, server->read_header_timeout_ns, client_timeout_handler, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto expires_fail;
	}

	client->http_private.finish_func = finish_request_line;
	err = client->bs.read_until(&client->bs, &client->rb, CIO_CRLF, parse, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto read_until_fail;
	}

	return;

read_until_fail:
expires_fail:
	client->http_private.request_timer.close(&client->http_private.request_timer);
request_timer_init_err:
	client->http_private.response_timer.close(&client->http_private.response_timer);
response_timer_init_err:
	handle_error(server, "client initialization failed");
	notify_free_handler_and_close_stream(client);
}

static void server_socket_closed(struct cio_server_socket *ss)
{
	struct cio_http_server *server = cio_container_of(ss, struct cio_http_server, server_socket);
	if (server->close_hook != NULL) {
		server->close_hook(server);
	}
}

static const unsigned int DEFAULT_BACKLOG = 5;

enum cio_error cio_http_server_init(struct cio_http_server *server,
                                    uint16_t port,
                                    struct cio_eventloop *loop,
                                    cio_http_serve_on_error on_error,
                                    uint64_t read_header_timeout_ns,
                                    uint64_t read_body_timeout_ns,
                                    uint64_t response_timeout_ns,
                                    cio_alloc_client alloc_client,
                                    cio_free_client free_client)
{
	if (cio_unlikely((server == NULL) || (port == 0) ||
	                 (loop == NULL) || (alloc_client == NULL) || (free_client == NULL) ||
	                 (read_header_timeout_ns == 0) || (read_body_timeout_ns == 0) || (response_timeout_ns == 0))) {
		return CIO_INVALID_ARGUMENT;
	}

	server->loop = loop;
	server->port = port;
	server->alloc_client = alloc_client;
	server->free_client = free_client;
	server->first_location = NULL;
	server->num_handlers = 0;
	server->on_error = on_error;
	server->read_header_timeout_ns = read_header_timeout_ns;
	server->read_body_timeout_ns = read_body_timeout_ns;
	server->response_timeout_ns = response_timeout_ns;
	server->close_hook = NULL;

	uint32_t keep_alive = (uint32_t)(read_header_timeout_ns / NANO_SECONDS_IN_SECONDS);
	snprintf(server->keepalive_header, sizeof(server->keepalive_header), "Keep-Alive: timeout=%" PRIu32 "\r\n", keep_alive);

	return cio_server_socket_init(&server->server_socket, server->loop, DEFAULT_BACKLOG, server->alloc_client, server->free_client, server_socket_closed);
}

enum cio_error cio_http_server_serve(struct cio_http_server *server)
{
	enum cio_error err = server->server_socket.set_reuse_address(&server->server_socket, true);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto close_socket;
	}

	err = server->server_socket.bind(&server->server_socket, NULL, server->port);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto close_socket;
	}

	err = cio_serversocket_accept(&server->server_socket, handle_accept, server);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto close_socket;
	}

	return err;

close_socket:
	server->server_socket.close(&server->server_socket);
	return err;
}


enum cio_error cio_http_server_register_location(struct cio_http_server *server, struct cio_http_location *location)
{
	if (cio_unlikely(server == NULL) || (location == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	location->next = server->first_location;
	server->first_location = location;
	server->num_handlers++;
	return CIO_SUCCESS;
}

enum cio_error cio_http_server_shutdown(struct cio_http_server *server, cio_http_server_close_hook close_hook)
{
	server->close_hook = close_hook;
	server->server_socket.close(&server->server_socket);
	return CIO_SUCCESS;
}
