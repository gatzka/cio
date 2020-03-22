/*
 * SPDX-License-Identifier: MIT
 *
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

#include <stdlib.h>
#include <string.h>

#include "fff.h"
#include "unity.h"

#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_http_location.h"
#include "cio_http_location_handler.h"
#include "cio_http_server.h"
#include "cio_server_socket.h"
#include "cio_timer.h"
#include "cio_util.h"
#include "cio_write_buffer.h"

#include "http-parser/http_parser.h"

#undef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define CRLF "\r\n"

struct request_test {
	cio_http_data_cb on_scheme;
	cio_http_data_cb on_host;
	cio_http_data_cb on_port;
	cio_http_data_cb on_path;
	cio_http_data_cb on_query;
	cio_http_data_cb on_fragment;
	cio_http_data_cb on_url;
	cio_http_data_cb on_header_field;
	cio_http_data_cb on_header_value;
	cio_http_cb on_header_complete;
	cio_http_data_cb on_body;
	cio_http_cb on_message_complete;
	cio_http_alloc_handler alloc_handler;
	enum cio_http_cb_return callback_return;
	int expected_response;
};

static const uint64_t header_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t body_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t response_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t close_timeout_ns = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

struct dummy_handler {
	struct cio_http_location_handler handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
};

static struct cio_socket *client_socket;

static const size_t read_buffer_size = 200;

DEFINE_FFF_GLOBALS

static void serve_error(struct cio_http_server *server, const char *reason);
FAKE_VOID_FUNC(serve_error, struct cio_http_server *, const char *)

FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_init, struct cio_server_socket *, struct cio_eventloop *, unsigned int, enum cio_address_family, cio_alloc_client, cio_free_client, uint64_t, cio_server_socket_close_hook)
FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_accept, struct cio_server_socket *, cio_accept_handler, void *)
FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_bind, struct cio_server_socket *, const struct cio_socket_address *)
FAKE_VOID_FUNC(cio_server_socket_close, struct cio_server_socket *)
FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_set_reuse_address, struct cio_server_socket *, bool)

FAKE_VALUE_FUNC(struct cio_io_stream *, cio_socket_get_io_stream, struct cio_socket *)

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)
FAKE_VALUE_FUNC(enum cio_error, cio_timer_cancel, struct cio_timer *)
FAKE_VOID_FUNC(cio_timer_close, struct cio_timer *)
FAKE_VALUE_FUNC(enum cio_error, cio_timer_expires_from_now, struct cio_timer *, uint64_t, cio_timer_handler, void *)

FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_init, struct cio_buffered_stream *, struct cio_io_stream *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_read_until, struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_read_at_least, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_close, struct cio_buffered_stream *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler, void *)

FAKE_VALUE_FUNC(enum cio_error, cio_init_inet_socket_address, struct cio_socket_address *, const struct cio_inet_address *, uint16_t)
FAKE_VALUE_FUNC0(const struct cio_inet_address *, cio_get_inet_address_any4)

FAKE_VALUE_FUNC(enum cio_address_family, cio_socket_address_get_family, const struct cio_socket_address *)

FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_set_tcp_fast_open, struct cio_server_socket *, bool)

FAKE_VOID_FUNC(http_close_hook, struct cio_http_server *)

static enum cio_http_cb_return on_message_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_message_complete, struct cio_http_client *)
static enum cio_http_cb_return on_header_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_header_complete, struct cio_http_client *)
static enum cio_http_cb_return on_header_field(struct cio_http_client *, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_header_field, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_header_value(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_header_value, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_url(struct cio_http_client *c, const char *at, size_t length);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_url, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_body(struct cio_http_client *c, const char *at, size_t length);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_body, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_schema(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_schema, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_host(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_host, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_port(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_port, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_path(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_path, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_query(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_query, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_fragment(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_fragment, struct cio_http_client *, const char *, size_t)

static enum cio_http_cb_return header_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, header_complete, struct cio_http_client *)
static enum cio_http_cb_return message_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, message_complete, struct cio_http_client *)

FAKE_VOID_FUNC(response_written_cb, struct cio_http_client *, enum cio_error)
FAKE_VOID_FUNC0(location_handler_called)
FAKE_VOID_FUNC0(sub_location_handler_called)

FAKE_VALUE_FUNC(enum cio_error, stream_close, struct cio_io_stream *)

struct cio_io_stream dummy_stream = {
    .close = stream_close};

static struct cio_io_stream *get_dummy_io_stream(struct cio_socket *s)
{
	(void)s;
	return &dummy_stream;
}

static enum cio_error cancel_timer(struct cio_timer *t)
{
	t->handler(t, t->handler_context, CIO_OPERATION_ABORTED);
	return CIO_SUCCESS;
}

static enum cio_error cancel_timer_error(struct cio_timer *t)
{
	(void)t;
	return CIO_INVALID_ARGUMENT;
}

static enum cio_error expires(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context)
{
	(void)timeout_ns;
	t->handler = handler;
	t->handler_context = handler_context;
	return CIO_SUCCESS;
}

static enum cio_error expires_error(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context)
{
	(void)timeout_ns;
	t->handler = handler;
	t->handler_context = handler_context;
	return CIO_INVALID_ARGUMENT;
}

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *loop, cio_timer_close_hook hook)
{
	(void)loop;
	timer->close_hook = hook;
	return CIO_SUCCESS;
}

static enum cio_error cio_timer_init_fails(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook)
{
	(void)l;
	(void)timer;
	(void)hook;
	return CIO_INVALID_ARGUMENT;
}

static enum cio_error cio_server_socket_init_ok(struct cio_server_socket *ss,
                                                struct cio_eventloop *loop,
                                                unsigned int backlog,
                                                enum cio_address_family family,
                                                cio_alloc_client alloc_client,
                                                cio_free_client free_client,
                                                uint64_t close_timeout,
                                                cio_server_socket_close_hook close_hook)
{
	(void)family;
	(void)close_timeout;
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->backlog = (int)backlog;
	ss->impl.loop = loop;
	ss->close_hook = close_hook;
	return CIO_SUCCESS;
}

static enum cio_error cio_server_socket_init_fails(struct cio_server_socket *ss,
                                                   struct cio_eventloop *l,
                                                   unsigned int backlog,
                                                   enum cio_address_family family,
                                                   cio_alloc_client alloc_client,
                                                   cio_free_client free_client,
                                                   uint64_t close_tout_ns,
                                                   cio_server_socket_close_hook close_hook)
{
	(void)ss;
	(void)l;
	(void)backlog;
	(void)family;
	(void)alloc_client;
	(void)free_client;
	(void)close_tout_ns;
	(void)close_hook;

	return CIO_INVALID_ARGUMENT;
}

static void bs_init(struct cio_buffered_stream *bs)
{
	bs->callback_is_running = 0;
	bs->shall_close = false;
	cio_write_buffer_head_init(&bs->wbh);
}

static struct cio_socket *client_socket = NULL;

static void free_dummy_client(struct cio_socket *socket)
{
	struct cio_http_client *client = cio_container_of(socket, struct cio_http_client, socket);
	free(client);
}

static struct cio_socket *alloc_dummy_client(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	if (client == NULL) {
		return NULL;
	}

	memset(client, 0xaf, sizeof(*client) + read_buffer_size);
	client->buffer_size = read_buffer_size;
	client->socket.close_hook = free_dummy_client;
	bs_init(&client->bs);
	client_socket = &client->socket;
	return &client->socket;
}

static struct cio_socket *alloc_dummy_client_no_buffer(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + 0);
	memset(client, 0xaf, sizeof(*client));
	client->buffer_size = 0;
	client->socket.close_hook = free_dummy_client;
	return &client->socket;
}

static struct cio_io_stream *get_null_io_stream(struct cio_socket *context)
{
	(void)context;
	return NULL;
}

static struct cio_socket *alloc_dummy_client_no_iostream(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	memset(client, 0xaf, sizeof(*client));
	client->buffer_size = read_buffer_size;
	cio_socket_get_io_stream_fake.custom_fake = get_null_io_stream;
	client->socket.close_hook = free_dummy_client;
	return &client->socket;
}

static void free_dummy_handler(struct cio_http_location_handler *handler)
{
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	free(dh);
}

static enum cio_http_cb_return callback_write_response(struct cio_http_client *c, enum cio_http_status_code code)
{
	static const char data[] = "Hello World!";
	struct cio_http_location_handler *handler = c->current_handler;
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_const_element_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	c->write_response(c, code, &dh->wbh, response_written_cb);
	return CIO_HTTP_CB_SUCCESS;
}

static enum cio_http_cb_return callback_write_ok_response(struct cio_http_client *c)
{
	return callback_write_response(c, CIO_HTTP_STATUS_OK);
}

static enum cio_http_cb_return message_complete_write_response(struct cio_http_client *c)
{
	static const char data[] = "Hello World!";
	struct cio_http_location_handler *handler = c->current_handler;
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_const_element_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	c->write_response(c, CIO_HTTP_STATUS_OK, &dh->wbh, NULL);
	return CIO_HTTP_CB_SUCCESS;
}

static enum cio_http_cb_return callback_write_not_found_response(struct cio_http_client *c)
{
	return callback_write_response(c, CIO_HTTP_STATUS_NOT_FOUND);
}

static enum cio_http_cb_return data_callback_write_response(struct cio_http_client *c, const char *at, size_t length)
{
	(void)at;
	(void)length;

	static const char data[] = "Hello World!";
	struct cio_http_location_handler *handler = c->current_handler;
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_const_element_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	c->write_response(c, CIO_HTTP_STATUS_NOT_FOUND, &dh->wbh, NULL);
	return CIO_HTTP_CB_SUCCESS;
}

static struct cio_http_location_handler *alloc_dummy_handler(const void *config)
{
	uintptr_t location = (uintptr_t)config;
	if (location == 0) {
		location_handler_called();
	} else if (location == 1) {
		sub_location_handler_called();
	}

	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_http_location_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		handler->handler.on_headers_complete = header_complete;
		handler->handler.on_message_complete = message_complete;
		return &handler->handler;
	}
}

static struct cio_http_location_handler *alloc_handler_for_callback_test(const void *config)
{
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	} else {
		const struct request_test *test = (const struct request_test *)config;
		cio_http_location_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		handler->handler.on_header_field = test->on_header_field;
		handler->handler.on_header_value = test->on_header_value;
		handler->handler.on_url = test->on_url;
		handler->handler.on_headers_complete = test->on_header_complete;
		handler->handler.on_body = test->on_body;
		handler->handler.on_message_complete = test->on_message_complete;
		handler->handler.on_schema = test->on_scheme;
		handler->handler.on_host = test->on_host;
		handler->handler.on_port = test->on_port;
		handler->handler.on_path = test->on_path;
		handler->handler.on_query = test->on_query;
		handler->handler.on_fragment = test->on_fragment;

		on_schema_fake.return_val = test->callback_return;
		on_host_fake.return_val = test->callback_return;
		on_port_fake.return_val = test->callback_return;
		on_path_fake.return_val = test->callback_return;
		on_query_fake.return_val = test->callback_return;
		on_fragment_fake.return_val = test->callback_return;

		return &handler->handler;
	}
}

static enum cio_http_cb_return on_header_complete_upgrade(struct cio_http_client *c)
{
	static const char data[] = "Hello World!";
	struct cio_http_location_handler *handler = c->current_handler;
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_const_element_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	c->write_response(c, CIO_HTTP_STATUS_SWITCHING_PROTOCOLS, &dh->wbh, NULL);
	return CIO_HTTP_CB_SKIP_BODY;
}

static struct cio_http_location_handler *alloc_upgrade_handler(const void *config)
{
	uintptr_t location = (uintptr_t)config;
	if (location == 0) {
		location_handler_called();
	} else if (location == 1) {
		sub_location_handler_called();
	}
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_http_location_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		handler->handler.on_headers_complete = on_header_complete_upgrade;
		return &handler->handler;
	}
}

static struct cio_http_location_handler *alloc_failing_handler(const void *config)
{
	(void)config;
	return NULL;
}

static struct dummy_handler handler_with_no_free = {
    .handler = {.free = NULL}};

static struct cio_http_location_handler *alloc_handler_with_no_free(const void *config)
{
	(void)config;

	return &handler_with_no_free.handler;
}

static size_t num_of_request_lines;
static unsigned int current_line;

static char line_buffer[10][100];

static enum cio_error bs_read_until_ok(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)delim;

	if (current_line >= num_of_request_lines) {
		handler(bs, handler_context, CIO_EOF, buffer, 0);
	} else {
		const char *line = line_buffer[current_line];
		size_t length = strlen(line);
		memcpy(buffer->add_ptr, line, length);
		buffer->add_ptr += length;
		current_line++;
		handler(bs, handler_context, CIO_SUCCESS, buffer, length);
	}

	return CIO_SUCCESS;
}

static enum cio_error bs_read_until_blocks(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)delim;
	(void)handler;
	(void)handler_context;

	return CIO_SUCCESS;
}

static enum cio_error bs_read_until_call_fails(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)delim;
	(void)handler;
	(void)handler_context;

	return CIO_BAD_FILE_DESCRIPTOR;
}

static enum cio_error bs_read_until_error_in_callback(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)delim;

	handler(bs, handler_context, CIO_BAD_FILE_DESCRIPTOR, buffer, 0);
	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_ok(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{

	memset(buffer->add_ptr, 'a', num);
	buffer->add_ptr += num;
	handler(bs, handler_context, CIO_SUCCESS, buffer, num);

	return CIO_SUCCESS;
}

static enum cio_error bs_read_at_least_call_fails(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;
	(void)handler;
	(void)handler_context;
	return CIO_BAD_FILE_DESCRIPTOR;
}

static enum cio_error bs_close_ok(struct cio_buffered_stream *bs)
{
	(void)bs;
	stream_close(NULL);
	return CIO_SUCCESS;
}

static enum cio_error bs_close_fails(struct cio_buffered_stream *bs)
{
	(void)bs;
	return CIO_BAD_FILE_DESCRIPTOR;
}

static uint8_t write_buffer[1000];
static size_t write_pos;

static cio_buffered_stream_write_handler blocked_write_handler;
static void *blocked_write_handler_context;
static struct cio_buffered_stream *blocked_write_bs;

static enum cio_error bs_write_all(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	size_t buffer_len = cio_write_buffer_get_num_buffer_elements(buf);
	const struct cio_write_buffer *data_buf = buf;

	for (unsigned int i = 0; i < buffer_len; i++) {
		data_buf = data_buf->next;
		memcpy(&write_buffer[write_pos], data_buf->data.element.const_data, data_buf->data.element.length);
		write_pos += data_buf->data.element.length;
	}

	handler(bs, handler_context, CIO_SUCCESS);
	return CIO_SUCCESS;
}

static enum cio_error bs_write_error_in_callback(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)buf;
	handler(bs, handler_context, CIO_ADDRESS_IN_USE);
	return CIO_SUCCESS;
}

static enum cio_error bs_write_error(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)bs;
	(void)buf;
	(void)handler;
	(void)handler_context;

	return CIO_BAD_FILE_DESCRIPTOR;
}

static enum cio_error bs_write_blocks(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	size_t buffer_len = cio_write_buffer_get_num_buffer_elements(buf);
	const struct cio_write_buffer *data_buf = buf;

	for (unsigned int i = 0; i < buffer_len; i++) {
		data_buf = data_buf->next;
		memcpy(&write_buffer[write_pos], data_buf->data.element.const_data, data_buf->data.element.length);
		write_pos += data_buf->data.element.length;
	}

	blocked_write_handler = handler;
	blocked_write_handler_context = handler_context;
	blocked_write_bs = bs;

	return CIO_SUCCESS;
}

static struct cio_eventloop loop;

static http_parser parser;
static http_parser_settings parser_settings;

static void check_http_response(int status_code)
{
	size_t nparsed = http_parser_execute(&parser, &parser_settings, (const char *)write_buffer, write_pos);
	TEST_ASSERT_EQUAL_MESSAGE(write_pos, nparsed, "Not a valid http response!");
	TEST_ASSERT_EQUAL_MESSAGE(status_code, parser.status_code, "http response status code not correct!");
}

static void close_server_socket(struct cio_server_socket *ss)
{
	if (ss->close_hook != NULL) {
		ss->close_hook(ss);
	}
}

static enum cio_error accept_call_handler(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	handler(ss, handler_context, CIO_SUCCESS, client_socket);
	return CIO_SUCCESS;
}

static enum cio_error accept_error_handler(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	handler(ss, handler_context, CIO_INVALID_ARGUMENT, client_socket);
	return CIO_SUCCESS;
}

static void split_request(const char *request)
{
	num_of_request_lines = 0;
	const char *from = request;
	const char *to = strchr(request, '\n');
	while (to != NULL) {
		ptrdiff_t bytes_to_copy = to - from + 1;
		memcpy(line_buffer[num_of_request_lines], from, (size_t)bytes_to_copy);
		line_buffer[num_of_request_lines][bytes_to_copy] = '\0';
		num_of_request_lines++;
		to++;
		from = to;
		to = strchr(to, '\n');
	}
}

static void fire_keepalive_timeout(struct cio_socket *s)
{
	struct cio_http_client *client = cio_container_of(s, struct cio_http_client, socket);
	client->http_private.request_timer.handler(&client->http_private.request_timer, client->http_private.request_timer.handler_context, CIO_SUCCESS);
}

static enum cio_error stream_close_free(struct cio_io_stream *s)
{
	(void)s;
	free_dummy_client(client_socket);
	return CIO_SUCCESS;
}

void setUp(void)
{
	FFF_RESET_HISTORY()

	RESET_FAKE(cio_buffered_stream_close)
	RESET_FAKE(cio_buffered_stream_init)
	RESET_FAKE(cio_buffered_stream_read_at_least)
	RESET_FAKE(cio_buffered_stream_read_until)
	RESET_FAKE(cio_buffered_stream_write)

	RESET_FAKE(cio_server_socket_accept)
	RESET_FAKE(cio_server_socket_bind)
	RESET_FAKE(cio_server_socket_close)
	RESET_FAKE(cio_server_socket_init)
	RESET_FAKE(cio_server_socket_set_reuse_address)

	RESET_FAKE(cio_socket_get_io_stream)

	RESET_FAKE(cio_timer_init)
	RESET_FAKE(cio_timer_cancel)
	RESET_FAKE(cio_timer_close)
	RESET_FAKE(cio_timer_expires_from_now)

	RESET_FAKE(header_complete)
	RESET_FAKE(message_complete)
	RESET_FAKE(serve_error)

	RESET_FAKE(http_close_hook)
	RESET_FAKE(on_schema)
	RESET_FAKE(on_host)
	RESET_FAKE(on_port)
	RESET_FAKE(on_path)
	RESET_FAKE(on_query)
	RESET_FAKE(on_fragment)
	RESET_FAKE(on_url)
	RESET_FAKE(on_header_field)
	RESET_FAKE(on_header_value)
	RESET_FAKE(on_header_complete)
	RESET_FAKE(on_body)
	RESET_FAKE(on_message_complete)

	RESET_FAKE(response_written_cb)

	RESET_FAKE(location_handler_called)
	RESET_FAKE(sub_location_handler_called)

	RESET_FAKE(stream_close)
	stream_close_fake.custom_fake = stream_close_free;

	http_parser_settings_init(&parser_settings);
	http_parser_init(&parser, HTTP_RESPONSE);

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
	cio_timer_expires_from_now_fake.custom_fake = expires;
	cio_timer_cancel_fake.custom_fake = cancel_timer;

	cio_buffered_stream_read_until_fake.custom_fake = bs_read_until_ok;
	cio_buffered_stream_read_at_least_fake.custom_fake = bs_read_at_least_ok;
	cio_buffered_stream_close_fake.custom_fake = bs_close_ok;
	current_line = 0;
	memset(write_buffer, 0xaf, sizeof(write_buffer));
	write_pos = 0;

	cio_buffered_stream_write_fake.custom_fake = bs_write_all;
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	cio_server_socket_close_fake.custom_fake = close_server_socket;

	cio_socket_get_io_stream_fake.custom_fake = get_dummy_io_stream;

	client_socket = alloc_dummy_client();

	cio_server_socket_accept_fake.custom_fake = accept_call_handler;

	memset(line_buffer, 'A', sizeof line_buffer);
}

void tearDown(void)
{
}

static void test_read_until_errors(void)
{
	struct test {
		unsigned int which_read_until_fails;
		int expected_response;
	};

	static const struct test tests[] = {
	    {.which_read_until_fails = 0, .expected_response = 0},
	    {.which_read_until_fails = 1, .expected_response = 500},
	    {.which_read_until_fails = 2, .expected_response = 500},
	    {.which_read_until_fails = 3, .expected_response = 500},
	};

	enum cio_error (*bs_read_until_fakes[ARRAY_SIZE(tests)])(struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *);
	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct test test = tests[i];

		unsigned int array_size = ARRAY_SIZE(bs_read_until_fakes);
		for (unsigned int j = 0; j < array_size; j++) {
			bs_read_until_fakes[j] = bs_read_until_ok;
		}

		cio_buffered_stream_read_until_fake.custom_fake = NULL;
		bs_read_until_fakes[test.which_read_until_fails] = bs_read_until_call_fails;
		SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_until, bs_read_until_fakes, (int)array_size)

		header_complete_fake.custom_fake = callback_write_ok_response;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = close_timeout_ns,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		const char *request = "GET /foo HTTP/1.1" CRLF "foo: bar" CRLF CRLF "GET /foo HTTP/1.1" CRLF "foo: bar" CRLF CRLF;

		split_request(request);

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
		check_http_response(test.expected_response);

		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_close_error(void)
{
	cio_buffered_stream_close_fake.custom_fake = bs_close_fails;

	header_complete_fake.custom_fake = callback_write_ok_response;

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = close_timeout_ns,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	const char *request = "GET /foo HTTP/1.1" CRLF CRLF;

	split_request(request);

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(200);
	// Because the response is written in on_headers_complete, on_message_complete will not be called
	TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
}

static void test_read_at_least_error(void)
{
	cio_buffered_stream_read_at_least_fake.custom_fake = bs_read_at_least_call_fails;

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = close_timeout_ns,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	const char *request = "GET /foo HTTP/1.1" CRLF "Content-Length: 5" CRLF CRLF "Hello"
	                      "GET /foo HTTP/1.1" CRLF "Content-Length: 5" CRLF CRLF "Hello";

	split_request(request);

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
	check_http_response(500);
}

static void test_write_error(void)
{
	header_complete_fake.custom_fake = callback_write_ok_response;
	cio_buffered_stream_write_fake.custom_fake = bs_write_error_in_callback;

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = close_timeout_ns,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	const char *request = "GET /foo HTTP/1.1" CRLF CRLF;

	split_request(request);

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was called!");
	// Because the response is written in on_headers_complete, on_message_complete will not be called
	TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
}

static void test_server_init(void)
{
	struct server_init_arguments {
		struct cio_http_server *server;
		struct cio_eventloop *loop;
		void (*serve_error)(struct cio_http_server *server, const char *reason);
		uint64_t header_read_timeout;
		uint64_t body_read_timeout;
		uint64_t response_timeout;
		struct cio_socket *(*alloc_client)(void);
		void (*free_client)(struct cio_socket *socket);
		enum cio_error (*server_socket_init)(struct cio_server_socket *ss, struct cio_eventloop *loop, unsigned int backlog, enum cio_address_family family, cio_alloc_client alloc_client, cio_free_client free_client, uint64_t close_timeout_ns, cio_server_socket_close_hook close_hook);
		enum cio_error expected_result;
	};

	struct cio_http_server server;
	struct server_init_arguments server_init_arguments[] = {
	    {.server = &server, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_SUCCESS},
	    {.server = NULL, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
	    {.server = &server, .loop = NULL, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
	    {.server = &server, .loop = &loop, .serve_error = NULL, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_SUCCESS},
	    {.server = &server, .loop = &loop, .serve_error = serve_error, .header_read_timeout = 0, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
	    {.server = &server, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = 0, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
	    {.server = &server, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = 0, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
	    {.server = &server, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = NULL, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
	    {.server = &server, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = NULL, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
	    {.server = &server, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_fails, .expected_result = CIO_INVALID_ARGUMENT},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(server_init_arguments); i++) {
		struct server_init_arguments args = server_init_arguments[i];
		cio_server_socket_init_fake.custom_fake = args.server_socket_init;

		struct cio_http_server_configuration config = {
		    .on_error = args.serve_error,
		    .read_header_timeout_ns = args.header_read_timeout,
		    .read_body_timeout_ns = args.body_read_timeout,
		    .response_timeout_ns = args.response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = args.alloc_client,
		    .free_client = args.free_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		enum cio_error err = cio_http_server_init(args.server, args.loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(args.expected_result, err, "Initialization failed!");

		free_dummy_client(client_socket);
		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_server_init_no_config(void)
{
	struct cio_http_server server;

	enum cio_error err = cio_http_server_init(&server, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Initialization failed!");

	free_dummy_client(client_socket);
}

static void test_shutdown(void)
{
	struct shutdown_args {
		void (*close_hook)(struct cio_http_server *server);
		unsigned int close_hook_call_count;
		enum cio_error expected_result;
	};

	struct shutdown_args shutdown_args[] = {
	    {.close_hook = NULL, .close_hook_call_count = 0, .expected_result = CIO_SUCCESS},
	    {.close_hook = http_close_hook, .close_hook_call_count = 1, .expected_result = CIO_SUCCESS},
	};

	struct cio_http_server server;

	for (unsigned int i = 0; i < ARRAY_SIZE(shutdown_args); i++) {
		struct shutdown_args args = shutdown_args[i];

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");
		err = cio_http_server_shutdown(&server, args.close_hook);
		TEST_ASSERT_EQUAL_MESSAGE(args.close_hook_call_count, http_close_hook_fake.call_count, "http close hook was not called correctly");
		TEST_ASSERT_EQUAL_MESSAGE(args.expected_result, err, "Server shutdown failed!");

		free_dummy_client(client_socket);
		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_register_request_target(void)
{
	struct register_request_target_args {
		struct cio_http_server *server;
		struct cio_http_location *target;
		enum cio_error expected_result;
	};

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = 10,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	struct register_request_target_args args[] = {
	    {.server = &server, .target = &target, .expected_result = CIO_SUCCESS},
	    {.server = NULL, .target = &target, .expected_result = CIO_INVALID_ARGUMENT},
	    {.server = &server, .target = NULL, .expected_result = CIO_INVALID_ARGUMENT},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(args); i++) {
		struct register_request_target_args arg = args[i];
		err = cio_http_server_register_location(arg.server, arg.target);
		TEST_ASSERT_EQUAL_MESSAGE(arg.expected_result, err, "Register request target not handled correctly!");

		free_dummy_client(client_socket);
		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_serve_locations(void)
{
	struct location_test {
		const char *location;
		const char *sub_location;
		const char *request_target;
		int expected_response;
		unsigned int location_call_count;
		unsigned int sub_location_call_count;
	};

	static const struct location_test location_tests[] = {
	    {.location = "/foo", .sub_location = "/foo/bar", .request_target = "GET /foo HTTP/1.1" CRLF CRLF, .expected_response = 200, .location_call_count = 1, .sub_location_call_count = 0},
	    {.location = "/foo", .sub_location = "/foo/bar", .request_target = "GET /foo/ HTTP/1.1" CRLF CRLF, .expected_response = 200, .location_call_count = 1, .sub_location_call_count = 0},
	    {.location = "/foo", .sub_location = "/foo/bar", .request_target = "GET /foo/bar HTTP/1.1" CRLF CRLF, .expected_response = 200, .location_call_count = 0, .sub_location_call_count = 1},
	    {.location = "/foo", .sub_location = "/foo/bar", .request_target = "GET /foo2 HTTP/1.1" CRLF CRLF, .expected_response = 404, .location_call_count = 0, .sub_location_call_count = 0},
	    {.location = "", .sub_location = "/foo/bar", .request_target = "GET /foo2 HTTP/1.1" CRLF CRLF, .expected_response = 404, .location_call_count = 0, .sub_location_call_count = 0},
	    {.location = "/foo/", .sub_location = "/foo/bar", .request_target = "GET /foo HTTP/1.1" CRLF CRLF, .expected_response = 404, .location_call_count = 0, .sub_location_call_count = 0},
	    {.location = "/foo/", .sub_location = "/foo/bar", .request_target = "GET /foo/ HTTP/1.1" CRLF CRLF, .expected_response = 200, .location_call_count = 1, .sub_location_call_count = 0},
	    {.location = "/foo/", .sub_location = "/foo/bar", .request_target = "GET /foo/bar HTTP/1.1" CRLF CRLF, .expected_response = 200, .location_call_count = 0, .sub_location_call_count = 1},
	    {.location = "/foo/", .sub_location = "/foo/bar", .request_target = "GET /foo2 HTTP/1.1" CRLF CRLF, .expected_response = 404, .location_call_count = 0, .sub_location_call_count = 0},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(location_tests); i++) {
		header_complete_fake.custom_fake = callback_write_ok_response;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct location_test location_test = location_tests[i];

		split_request(location_test.request_target);

		struct cio_http_location target1;
		uintptr_t loc_marker = 0;
		err = cio_http_location_init(&target1, location_test.location, (void *)loc_marker, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target1 initialization failed!");
		err = cio_http_server_register_location(&server, &target1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target1 failed!");

		struct cio_http_location target2;
		uintptr_t sub_loc_marker = 1;
		err = cio_http_location_init(&target2, location_test.sub_location, (void *)sub_loc_marker, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target2 initialization failed!");
		err = cio_http_server_register_location(&server, &target2);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target2 failed!");

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		check_http_response(location_test.expected_response);

		TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "on_message_complete was not called!");
		if (location_test.expected_response == 200) {
			TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "on_header_complete was not called!");
			TEST_ASSERT_EQUAL_MESSAGE(1, response_written_cb_fake.call_count, "response_written callback was not called!");
		}

		TEST_ASSERT_EQUAL_MESSAGE(location_test.location_call_count, location_handler_called_fake.call_count, "location handler was not called correctly");
		TEST_ASSERT_EQUAL_MESSAGE(location_test.sub_location_call_count, sub_location_handler_called_fake.call_count, "sub_location handler was not called correctly");

		TEST_ASSERT_EQUAL_MESSAGE(1, cio_buffered_stream_close_fake.call_count, "buffered stream was not closed!");

		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_keepalive_handling(void)
{
	struct keepalive_test {
		const char *location;
		const char *request;
		int expected_response;
		bool immediate_close;
	};

	static const struct keepalive_test keepalive_tests[] = {
	    {.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF "Content-Length: 0" CRLF CRLF CRLF, .expected_response = 200, .immediate_close = false},
	    {.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF "Content-Length: 0" CRLF "Connection: keep-alive" CRLF CRLF CRLF, .expected_response = 200, .immediate_close = false},
	    {.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF "Content-Length: 0" CRLF "Connection: close" CRLF CRLF CRLF, .expected_response = 200, .immediate_close = true},
	    {.location = "/foo", .request = "GET /foo HTTP/1.0" CRLF "Content-Length: 0" CRLF CRLF CRLF, .expected_response = 200, .immediate_close = true},
	    {.location = "/foo", .request = "GET /foo HTTP/1.0" CRLF "Content-Length: 0" CRLF "Connection: keep-alive" CRLF CRLF CRLF, .expected_response = 200, .immediate_close = false},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(keepalive_tests); i++) {
		struct keepalive_test keepalive_test = keepalive_tests[i];
		split_request(keepalive_test.request);

		if (num_of_request_lines > 0) {
			enum cio_error (*bs_read_until_fakes[num_of_request_lines])(struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *);
			size_t array_size = ARRAY_SIZE(bs_read_until_fakes);
			for (unsigned int j = 0; j < array_size - 1; j++) {
				bs_read_until_fakes[j] = bs_read_until_ok;
			}
			bs_read_until_fakes[array_size - 1] = bs_read_until_blocks;
			cio_buffered_stream_read_until_fake.custom_fake = NULL;
			SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_until, bs_read_until_fakes, (int)array_size)

			header_complete_fake.custom_fake = callback_write_ok_response;

			struct cio_http_server_configuration config = {
			    .on_error = serve_error,
			    .read_header_timeout_ns = header_read_timeout,
			    .read_body_timeout_ns = body_read_timeout,
			    .response_timeout_ns = response_timeout,
			    .close_timeout_ns = 10,
			    .alloc_client = alloc_dummy_client,
			    .free_client = free_dummy_client};

			cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

			struct cio_http_server server;
			enum cio_error err = cio_http_server_init(&server, &loop, &config);
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

			struct cio_http_location target;
			err = cio_http_location_init(&target, keepalive_test.location, NULL, alloc_dummy_handler);
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
			err = cio_http_server_register_location(&server, &target);
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

			err = cio_http_server_serve(&server);
			TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

			TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
			check_http_response(keepalive_test.expected_response);

			if (!keepalive_test.immediate_close) {
				TEST_ASSERT_EQUAL_MESSAGE(0, cio_buffered_stream_close_fake.call_count, "buffered stream was closed before keepalive timeout triggered!");
				fire_keepalive_timeout(client_socket);
			}

			TEST_ASSERT_EQUAL_MESSAGE(1, cio_buffered_stream_close_fake.call_count, "client socket was not closed after keepalive timeout triggered!");
			setUp();
		}
	}

	free_dummy_client(client_socket);
}

static void test_callbacks_after_response_sent(void)
{
	enum callback_sends_response {
		NO_RESPONSE = 0,
		ON_SCHEMA_SENDS_RESPONSE,
		ON_HOST_SENDS_RESPONSE,
		ON_PORT_SENDS_RESPONSE,
		ON_PATH_SENDS_RESPONSE,
		ON_QUERY_SENDS_RESPONSE,
		ON_FRAGMENT_SENDS_RESPONSE,
		ON_URL_SENDS_RESPONSE,
		ON_HEADER_FIELD_SENDS_RESPONSE,
		ON_HEADER_VALUE_SENDS_RESPONSE,
		ON_HEADER_COMPLETE_SENDS_RESPONSE,
		ON_BODY_SENDS_RESPONSE,
		ON_MESSAGE_COMPLETE_SENDS_RESPONSE
	};

	struct tests {
		const char *request;
		enum callback_sends_response who_sends_response;
		int expected_response;
		bool keep_alive;
	};
	struct tests tests[] = {
	    {.expected_response = 500, .who_sends_response = NO_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 7" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_SCHEMA_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_HOST_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_PORT_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_PATH_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_QUERY_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_FRAGMENT_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_URL_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_HEADER_FIELD_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_HEADER_VALUE_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_BODY_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF "Hello", .keep_alive = false},
	    {.expected_response = 200, .who_sends_response = ON_HEADER_COMPLETE_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 404, .who_sends_response = ON_MESSAGE_COMPLETE_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = false},
	    {.expected_response = 200, .who_sends_response = ON_HEADER_COMPLETE_SENDS_RESPONSE, .request = "GET http://172.19.1.1:8080/foo?search=qry#fraggy HTTP/1.1" CRLF "Content-Length: 5" CRLF CRLF, .keep_alive = true},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct tests test = tests[i];

		struct request_test callbacks = {
		    .on_scheme = on_schema,
		    .on_host = on_host,
		    .on_port = on_port,
		    .on_path = on_path,
		    .on_query = on_query,
		    .on_fragment = on_fragment,
		    .on_url = on_url,
		    .on_header_field = on_header_field,
		    .on_header_value = on_header_value,
		    .on_header_complete = on_header_complete,
		    .on_body = on_body,
		    .on_message_complete = on_message_complete,
		};

		enum cio_error (*bs_read_until_fakes[4])(struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *);
		size_t array_size = ARRAY_SIZE(bs_read_until_fakes);
		for (unsigned int j = 0; j < array_size - 1; j++) {
			bs_read_until_fakes[j] = bs_read_until_ok;
		}
		bs_read_until_fakes[array_size - 1] = bs_read_until_blocks;
		if (test.keep_alive) {
			cio_buffered_stream_read_until_fake.custom_fake = NULL;
			SET_CUSTOM_FAKE_SEQ(cio_buffered_stream_read_until, bs_read_until_fakes, (int)array_size)
		}

		if (test.who_sends_response == ON_SCHEMA_SENDS_RESPONSE)
			on_schema_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_HOST_SENDS_RESPONSE)
			on_host_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_PORT_SENDS_RESPONSE)
			on_port_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_PATH_SENDS_RESPONSE)
			on_path_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_QUERY_SENDS_RESPONSE)
			on_query_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_FRAGMENT_SENDS_RESPONSE)
			on_fragment_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_URL_SENDS_RESPONSE)
			on_url_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_HEADER_FIELD_SENDS_RESPONSE)
			on_header_field_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_HEADER_VALUE_SENDS_RESPONSE)
			on_header_value_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_BODY_SENDS_RESPONSE)
			on_body_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_HEADER_COMPLETE_SENDS_RESPONSE)
			on_header_complete_fake.custom_fake = callback_write_ok_response;
		if (test.who_sends_response == ON_MESSAGE_COMPLETE_SENDS_RESPONSE)
			on_message_complete_fake.custom_fake = callback_write_not_found_response;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", &callbacks, alloc_handler_for_callback_test);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		split_request(test.request);

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");
		TEST_ASSERT_EQUAL_MESSAGE(test.expected_response == 500 ? 1 : 0, serve_error_fake.call_count, "Serve error callback was called!");

		check_http_response(test.expected_response);

		switch (test.who_sends_response) {
		case ON_MESSAGE_COMPLETE_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_message_complete_fake.call_count, "on_message_complete was not called!");
			/* FALLTHRU */
		case ON_BODY_SENDS_RESPONSE:
			TEST_ASSERT_GREATER_THAN_MESSAGE(0, on_body_fake.call_count, "on_body was not called!");
			/* FALLTHRU */
		case ON_HEADER_COMPLETE_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_header_complete_fake.call_count, "on_header_complete was not called!");
			/* FALLTHRU */
		case ON_HEADER_VALUE_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_header_value_fake.call_count, "on_header_value was not called!");
			/* FALLTHRU */
		case ON_HEADER_FIELD_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_header_field_fake.call_count, "on_header_field was not called!");
			/* FALLTHRU */
		case ON_URL_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_url_fake.call_count, "on_url was not called!");
			/* FALLTHRU */
		case ON_FRAGMENT_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_fragment_fake.call_count, "on_fragment was not called!");
			/* FALLTHRU */
		case ON_QUERY_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_query_fake.call_count, "on_query was not called!");
			/* FALLTHRU */
		case ON_PATH_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_path_fake.call_count, "on_path was not called!");
			/* FALLTHRU */
		case ON_PORT_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_port_fake.call_count, "on_port was not called!");
			/* FALLTHRU */
		case ON_HOST_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_host_fake.call_count, "on_host was not called!");
			/* FALLTHRU */
		case ON_SCHEMA_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(1, on_schema_fake.call_count, "on_schema was not called!");
			break;

		default:
			break;
		}

		switch (test.who_sends_response) {
		case ON_SCHEMA_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_host_fake.call_count, "on_host was called!");
			/* FALLTHRU */
		case ON_HOST_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_port_fake.call_count, "on_port was called!");
			/* FALLTHRU */
		case ON_PORT_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_path_fake.call_count, "on_path was called!");
			/* FALLTHRU */
		case ON_PATH_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_query_fake.call_count, "on_query was called!");
			/* FALLTHRU */
		case ON_QUERY_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_fragment_fake.call_count, "on_fragment was called!");
			/* FALLTHRU */
		case ON_FRAGMENT_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_url_fake.call_count, "on_url was called!");
			/* FALLTHRU */
		case ON_URL_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_header_field_fake.call_count, "on_header_field was called!");
			/* FALLTHRU */
		case ON_HEADER_FIELD_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_header_value_fake.call_count, "on_header_value was called!");
			/* FALLTHRU */
		case ON_HEADER_VALUE_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_header_complete_fake.call_count, "on_header_complete was called!");
			/* FALLTHRU */
		case ON_HEADER_COMPLETE_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_body_fake.call_count, "on_body was called!");
			/* FALLTHRU */
		case ON_BODY_SENDS_RESPONSE:
			TEST_ASSERT_EQUAL_MESSAGE(0, on_message_complete_fake.call_count, "on_message_complete was called!");
			break;

		default:
			break;
		}

		if (test.keep_alive) {
			fire_keepalive_timeout(client_socket);
		}

		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_url_callbacks(void)
{
	static const struct request_test request_tests[] = {
	    {.on_scheme = on_schema, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_SUCCESS, .expected_response = 200},
	    {.on_host = on_host, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_SUCCESS, .expected_response = 200},
	    {.on_port = on_port, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_SUCCESS, .expected_response = 200},
	    {.on_path = on_path, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_SUCCESS, .expected_response = 200},
	    {.on_query = on_query, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_SUCCESS, .expected_response = 200},
	    {.on_fragment = on_fragment, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_SUCCESS, .expected_response = 200},
	    {.on_scheme = on_schema, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_ERROR, .expected_response = 400},
	    {.on_host = on_host, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_ERROR, .expected_response = 400},
	    {.on_port = on_port, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_ERROR, .expected_response = 400},
	    {.on_path = on_path, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_ERROR, .expected_response = 400},
	    {.on_query = on_query, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_ERROR, .expected_response = 400},
	    {.on_fragment = on_fragment, .on_message_complete = message_complete_write_response, .alloc_handler = alloc_handler_for_callback_test, .callback_return = CIO_HTTP_CB_ERROR, .expected_response = 400},
	    {.alloc_handler = alloc_handler_for_callback_test, .expected_response = 500},
	    {.on_scheme = on_schema, .alloc_handler = alloc_handler_for_callback_test, .expected_response = 500},
	    {.alloc_handler = alloc_failing_handler, .expected_response = 500},
	    {.alloc_handler = alloc_handler_with_no_free, .expected_response = 500},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(request_tests); i++) {
		struct request_test request_test = request_tests[i];

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", &request_test, request_test.alloc_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		static const char *scheme = "http";
		static const char *host = "172.19.1.1";
		static const char *port = "8080";
		static const char *path = "/foo";
		static const char *query = "search=qry";
		static const char *fragment = "fraggy";

		char buffer[200];
		snprintf(buffer, sizeof(buffer) - 1, "GET %s://%s:%s%s?%s#%s HTTP/1.1" CRLF "Content-Length: 5" CRLF CRLF "Hello", scheme, host, port, path, query, fragment);

		split_request(buffer);

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");
		TEST_ASSERT_EQUAL_MESSAGE(request_test.expected_response == 500 ? 1 : 0, serve_error_fake.call_count, "Serve error callback was called!");

		struct cio_http_client *client = cio_container_of(client_socket, struct cio_http_client, socket);

		if (request_test.on_scheme) {
			TEST_ASSERT_EQUAL_MESSAGE(1, on_schema_fake.call_count, "on_scheme callback was not called correctly!");
			TEST_ASSERT_EQUAL_MESSAGE(client, on_schema_fake.arg0_val, "'client' parameter in on_scheme not correct");
			TEST_ASSERT_EQUAL_MESSAGE(strlen(scheme), on_schema_fake.arg2_val, "'size' parameter in on_scheme not correct");
		}

		if (request_test.on_host) {
			TEST_ASSERT_EQUAL_MESSAGE(1, on_host_fake.call_count, "on_host callback was not called correctly!");
			TEST_ASSERT_EQUAL_MESSAGE(client, on_host_fake.arg0_val, "'client' parameter in on_host not correct");
			TEST_ASSERT_EQUAL_MESSAGE(strlen(host), on_host_fake.arg2_val, "'size' parameter in on_host not correct");
		}

		if (request_test.on_port) {
			TEST_ASSERT_EQUAL_MESSAGE(1, on_port_fake.call_count, "on_port callback was not called correctly!");
			TEST_ASSERT_EQUAL_MESSAGE(client, on_port_fake.arg0_val, "'client' parameter in on_port not correct");
			TEST_ASSERT_EQUAL_MESSAGE(strlen(port), on_port_fake.arg2_val, "'size' parameter in on_port not correct");
		}

		if (request_test.on_path) {
			TEST_ASSERT_EQUAL_MESSAGE(1, on_path_fake.call_count, "on_path callback was not called correctly!");
			TEST_ASSERT_EQUAL_MESSAGE(client, on_path_fake.arg0_val, "'client' parameter in on_path not correct");
			TEST_ASSERT_EQUAL_MESSAGE(strlen(path), on_path_fake.arg2_val, "'size' parameter in on_path not correct");
		}

		if (request_test.on_query) {
			TEST_ASSERT_EQUAL_MESSAGE(1, on_query_fake.call_count, "on_query callback was not called correctly!");
			TEST_ASSERT_EQUAL_MESSAGE(client, on_query_fake.arg0_val, "'client' parameter in on_query not correct");
			TEST_ASSERT_EQUAL_MESSAGE(strlen(query), on_query_fake.arg2_val, "'size' parameter in on_query not correct");
		}

		if (request_test.on_fragment) {
			TEST_ASSERT_EQUAL_MESSAGE(1, on_fragment_fake.call_count, "on_fragment callback was not called correctly!");
			TEST_ASSERT_EQUAL_MESSAGE(client, on_fragment_fake.arg0_val, "'client' parameter in on_fragment not correct");
			TEST_ASSERT_EQUAL_MESSAGE(strlen(fragment), on_fragment_fake.arg2_val, "'size' parameter in on_fragment not correct");
		}

		check_http_response(request_test.expected_response);

		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_errors_in_serve(void)
{
	struct serve_test {
		enum cio_error reuse_addres_retval;
		enum cio_error bind_retval;
		enum cio_error accept_retval;
	};

	static const struct serve_test serve_tests[] = {
	    {.reuse_addres_retval = CIO_INVALID_ARGUMENT, .bind_retval = CIO_SUCCESS, .accept_retval = CIO_SUCCESS},
	    {.reuse_addres_retval = CIO_SUCCESS, .bind_retval = CIO_INVALID_ARGUMENT, .accept_retval = CIO_SUCCESS},
	    {.reuse_addres_retval = CIO_SUCCESS, .bind_retval = CIO_SUCCESS, .accept_retval = CIO_INVALID_ARGUMENT},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(serve_tests); i++) {
		struct serve_test serve_test = serve_tests[i];

		cio_server_socket_set_reuse_address_fake.return_val = serve_test.reuse_addres_retval;
		cio_server_socket_bind_fake.return_val = serve_test.bind_retval;
		cio_server_socket_accept_fake.custom_fake = NULL;
		cio_server_socket_accept_fake.return_val = serve_test.accept_retval;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		err = cio_http_server_serve(&server);
		TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http did not fail!")
		TEST_ASSERT_EQUAL_MESSAGE(1, cio_server_socket_close_fake.call_count, "Close was not called!");

		free_dummy_client(client_socket);
		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_errors_in_accept(void)
{
	struct accept_test {
		struct cio_socket *(*alloc_client)(void);
		enum cio_error (*response_timer_init)(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook);
		enum cio_error (*request_timer_init)(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook);
		enum cio_error (*timer_expires)(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context);
		enum cio_error buffered_stream_init_return_val;
		enum cio_error (*accept_handler)(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context);

		const char *request;
	};

	static const struct accept_test accept_tests[] = {
	    {.accept_handler = accept_error_handler,
	     .alloc_client = alloc_dummy_client,
	     .response_timer_init = cio_timer_init_ok,
	     .request_timer_init = cio_timer_init_ok,
	     .timer_expires = expires,
	     .buffered_stream_init_return_val = CIO_SUCCESS,
	     .request = "GET /foo HTTP/1.1" CRLF CRLF},
	    {.accept_handler = accept_call_handler,
	     .alloc_client = alloc_dummy_client,
	     .response_timer_init = cio_timer_init_ok,
	     .request_timer_init = cio_timer_init_ok,
	     .timer_expires = expires,
	     .buffered_stream_init_return_val = CIO_NO_MEMORY,
	     .request = "GET /foo HTTP/1.1" CRLF CRLF},
	    {.accept_handler = accept_call_handler,
	     .alloc_client = alloc_dummy_client_no_buffer,
	     .response_timer_init = cio_timer_init_ok,
	     .request_timer_init = cio_timer_init_ok,
	     .timer_expires = expires,
	     .buffered_stream_init_return_val = CIO_SUCCESS,
	     .request = "GET /foo HTTP/1.1" CRLF CRLF},
	    {.accept_handler = accept_call_handler,
	     .alloc_client = alloc_dummy_client_no_iostream,
	     .response_timer_init = cio_timer_init_ok,
	     .request_timer_init = cio_timer_init_ok,
	     .timer_expires = expires,
	     .buffered_stream_init_return_val = CIO_SUCCESS,
	     .request = "GET /foo HTTP/1.1" CRLF CRLF},
	    {.accept_handler = accept_error_handler,
	     .alloc_client = alloc_dummy_client_no_iostream,
	     .response_timer_init = cio_timer_init_ok,
	     .request_timer_init = cio_timer_init_ok,
	     .timer_expires = expires,
	     .buffered_stream_init_return_val = CIO_SUCCESS,
	     .request = "GET /foo HTTP/1.1" CRLF CRLF},
	    {.accept_handler = accept_call_handler,
	     .alloc_client = alloc_dummy_client,
	     .response_timer_init = cio_timer_init_fails,
	     .request_timer_init = cio_timer_init_ok,
	     .timer_expires = expires,
	     .buffered_stream_init_return_val = CIO_SUCCESS,
	     .request = "GET /foo HTTP/1.1" CRLF CRLF},
	    {.accept_handler = accept_call_handler,
	     .alloc_client = alloc_dummy_client,
	     .response_timer_init = cio_timer_init_ok,
	     .request_timer_init = cio_timer_init_fails,
	     .timer_expires = expires,
	     .buffered_stream_init_return_val = CIO_SUCCESS,
	     .request = "GET /foo HTTP/1.1" CRLF CRLF},
	    {.accept_handler = accept_call_handler,
	     .alloc_client = alloc_dummy_client,
	     .response_timer_init = cio_timer_init_ok,
	     .request_timer_init = cio_timer_init_ok,
	     .timer_expires = expires_error,
	     .buffered_stream_init_return_val = CIO_SUCCESS,
	     .request = "GET /foo HTTP/1.1" CRLF CRLF},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(accept_tests); i++) {
		struct accept_test accept_test = accept_tests[i];
		cio_server_socket_accept_fake.custom_fake = accept_test.accept_handler;

		free_dummy_client(client_socket);
		client_socket = accept_test.alloc_client();

		enum cio_error (*timer_init_fakes[])(struct cio_timer * timer, struct cio_eventloop * l, cio_timer_close_hook hook) = {
		    accept_test.response_timer_init,
		    accept_test.request_timer_init};

		cio_timer_init_fake.custom_fake = NULL;
		SET_CUSTOM_FAKE_SEQ(cio_timer_init, timer_init_fakes, ARRAY_SIZE(timer_init_fakes))

		cio_timer_expires_from_now_fake.custom_fake = accept_test.timer_expires;
		cio_buffered_stream_init_fake.return_val = accept_test.buffered_stream_init_return_val;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = accept_test.alloc_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		split_request(accept_test.request);
		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");
		TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");

		if (accept_test.alloc_client != alloc_dummy_client_no_iostream) {
			TEST_ASSERT_EQUAL_MESSAGE(1, stream_close_fake.call_count, "stream was not closed!");
		}

		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_connection_upgrade(void)
{
	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = 10,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

	struct cio_http_server server;
	on_header_complete_fake.custom_fake = callback_write_ok_response;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_upgrade_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	split_request("GET /foo HTTP/1.1" CRLF "Upgrade: websocket" CRLF "Connection: Upgrade" CRLF CRLF);

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(101);
	fire_keepalive_timeout(client_socket);
}

static void test_parse_errors(void)
{
	struct parse_test {
		enum cio_error (*read_until)(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context);
		enum cio_error (*write_all)(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context);
		const char *request;
		int expected_response;
	};

	static struct parse_test parse_tests[] = {
	    {.read_until = bs_read_until_error_in_callback, .write_all = bs_write_all, .request = "GET /foo HTTP/1.1" CRLF CRLF, .expected_response = 500},
	    {.read_until = bs_read_until_ok, .write_all = bs_write_all, .request = "GT /foo HTTP/1.1" CRLF CRLF, .expected_response = 400},
	    {.read_until = bs_read_until_ok, .write_all = bs_write_error, .request = "GT /foo HTTP/1.1" CRLF CRLF},
	    {.read_until = bs_read_until_ok, .write_all = bs_write_all, .request = "GET http://ww%.google.de/ HTTP/1.1" CRLF CRLF, .expected_response = 400},
	    {.read_until = bs_read_until_ok, .write_all = bs_write_all, .request = "CONNECT www.google.de:80 HTTP/1.1" CRLF CRLF, .expected_response = 400},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(parse_tests); i++) {
		struct parse_test parse_test = parse_tests[i];

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		cio_buffered_stream_read_until_fake.custom_fake = parse_test.read_until;
		cio_buffered_stream_write_fake.custom_fake = parse_test.write_all;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		split_request(parse_test.request);

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		check_http_response(parse_test.expected_response);

		if (parse_test.expected_response == 500) {
			TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
		}

		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_timer_cancel_errors(void)
{
	enum cio_error (*timer_cancel_fakes[3])(struct cio_timer * timer);

	for (unsigned int i = 0; i < ARRAY_SIZE(timer_cancel_fakes); i++) {
		timer_cancel_fakes[0] = cancel_timer;
		timer_cancel_fakes[1] = cancel_timer;
		timer_cancel_fakes[2] = cancel_timer;

		timer_cancel_fakes[i] = cancel_timer_error;

		cio_timer_cancel_fake.custom_fake = NULL;
		SET_CUSTOM_FAKE_SEQ(cio_timer_cancel, timer_cancel_fakes, ARRAY_SIZE(timer_cancel_fakes))

		header_complete_fake.custom_fake = callback_write_ok_response;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		split_request("GET /foo HTTP/1.1" CRLF CRLF);
		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");

		TEST_ASSERT_EQUAL_MESSAGE(1, cio_buffered_stream_close_fake.call_count, "buffered stream was not closed after keepalive timeout triggered!");

		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_timer_expires_errors(void)
{
	enum cio_error (*timer_expires_fakes[4])(struct cio_timer * timer, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context);

	for (unsigned int i = 0; i < ARRAY_SIZE(timer_expires_fakes); i++) {
		timer_expires_fakes[0] = expires;
		timer_expires_fakes[1] = expires;
		timer_expires_fakes[2] = expires;
		timer_expires_fakes[3] = expires;

		timer_expires_fakes[i] = expires_error;

		cio_timer_expires_from_now_fake.custom_fake = NULL;
		SET_CUSTOM_FAKE_SEQ(cio_timer_expires_from_now, timer_expires_fakes, ARRAY_SIZE(timer_expires_fakes))

		header_complete_fake.custom_fake = callback_write_ok_response;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);
		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");
		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		split_request("GET /foo HTTP/1.1" CRLF CRLF);

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");
		TEST_ASSERT_GREATER_THAN_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
		TEST_ASSERT_EQUAL_MESSAGE(1, cio_buffered_stream_close_fake.call_count, "buffered stream was not closed!");

		setUp();
	}

	free_dummy_client(client_socket);
}

static void test_error_without_error_callback(void)
{
	struct cio_http_server_configuration config = {
	    .on_error = NULL,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = 10,
	    .alloc_client = alloc_dummy_client_no_buffer,
	    .free_client = free_dummy_client};

	cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	free_dummy_client(client_socket);
	client_socket = alloc_dummy_client_no_buffer();

	split_request("GET /foo HTTP/1.1" CRLF CRLF);

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was not called!");
}

static void test_response_callback_after_message_complete(void)
{
	on_header_complete_fake.custom_fake = message_complete_write_response;

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = 10,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), 8080);

	cio_buffered_stream_write_fake.custom_fake = bs_write_blocks;
	header_complete_fake.custom_fake = callback_write_ok_response;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	split_request("GET /foo HTTP/1.1" CRLF "Content-Length: 0" CRLF CRLF);

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	blocked_write_handler(blocked_write_bs, blocked_write_handler_context, CIO_SUCCESS);
	check_http_response(200);

	TEST_ASSERT_EQUAL_MESSAGE(1, cio_buffered_stream_close_fake.call_count, "buffered stream was not closed!");
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_read_until_errors);
	RUN_TEST(test_close_error);
	RUN_TEST(test_read_at_least_error);
	RUN_TEST(test_write_error);

	RUN_TEST(test_server_init);
	RUN_TEST(test_server_init_no_config);
	RUN_TEST(test_shutdown);

	RUN_TEST(test_register_request_target);
	RUN_TEST(test_serve_locations);
	RUN_TEST(test_keepalive_handling);
	RUN_TEST(test_callbacks_after_response_sent);
	RUN_TEST(test_url_callbacks);
	RUN_TEST(test_errors_in_serve);
	RUN_TEST(test_error_without_error_callback);
	RUN_TEST(test_errors_in_accept);
	RUN_TEST(test_parse_errors);

	RUN_TEST(test_connection_upgrade);

	RUN_TEST(test_timer_cancel_errors);
	RUN_TEST(test_timer_expires_errors);

	RUN_TEST(test_response_callback_after_message_complete);

	return UNITY_END();
}
