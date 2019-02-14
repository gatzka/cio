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

#include <stdlib.h>
#include <string.h>

#include "fff.h"
#include "unity.h"

#include "cio_error_code.h"
#include "cio_http_location.h"
#include "cio_http_location_handler.h"
#include "cio_http_server.h"
#include "cio_server_socket.h"
#include "cio_timer.h"
#include "cio_util.h"
#include "cio_write_buffer.h"

#include "http-parser/http_parser.h"

#undef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define ILLEGAL_REQUEST_TARGET "http://ww%.google.de/"
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

static struct cio_eventloop loop;

static const uint64_t header_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t body_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t response_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

struct memory_stream {
	struct cio_socket *socket;
	struct cio_io_stream ios;
	void *mem;
	size_t read_pos;
	size_t size;
	uint8_t write_buffer[1000];
	size_t write_pos;
};

static struct memory_stream ms;

struct dummy_handler {
	struct cio_http_location_handler handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
};

static const size_t read_buffer_size = 200;

DEFINE_FFF_GLOBALS

static void serve_error(struct cio_http_server *server, const char *reason);
FAKE_VOID_FUNC(serve_error, struct cio_http_server *, const char *)

static void socket_close(struct cio_server_socket *context);
FAKE_VOID_FUNC(socket_close, struct cio_server_socket *)
static enum cio_error socket_accept(struct cio_server_socket *context, cio_accept_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, socket_accept, struct cio_server_socket *, cio_accept_handler, void *)
static enum cio_error socket_set_reuse_address(struct cio_server_socket *context, bool on);
FAKE_VALUE_FUNC(enum cio_error, socket_set_reuse_address, struct cio_server_socket *, bool)
static enum cio_error socket_bind(struct cio_server_socket *context, const char *bind_address, uint16_t port);
FAKE_VALUE_FUNC(enum cio_error, socket_bind, struct cio_server_socket *, const char *, uint16_t)

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      cio_alloc_client alloc_client,
                                      cio_free_client free_client,
                                      cio_server_socket_close_hook close_hook);

FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_init, struct cio_server_socket *, struct cio_eventloop *, unsigned int, cio_alloc_client, cio_free_client, cio_server_socket_close_hook)

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

static void client_socket_close(void);
FAKE_VOID_FUNC0(client_socket_close)

static enum cio_error timer_cancel(struct cio_timer *t);
FAKE_VALUE_FUNC(enum cio_error, timer_cancel, struct cio_timer *)
static void timer_close(struct cio_timer *t);
FAKE_VOID_FUNC(timer_close, struct cio_timer *)
static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, timer_expires_from_now, struct cio_timer *, uint64_t, cio_timer_handler, void *)
FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)

FAKE_VOID_FUNC0(location_handler_called)
FAKE_VOID_FUNC0(sub_location_handler_called)

FAKE_VOID_FUNC(response_written_cb, struct cio_http_client *, enum cio_error)

static enum cio_error cio_timer_init_fails(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook)
{
	(void)l;
	(void)timer;
	(void)hook;
	return CIO_INVALID_ARGUMENT;
}

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook)
{
	(void)l;
	timer->cancel = timer_cancel;
	timer->close = timer_close;
	timer->close_hook = hook;
	timer->expires_from_now = timer_expires_from_now;
	return CIO_SUCCESS;
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

static enum cio_error cio_server_socket_init_ok(struct cio_server_socket *ss,
												struct cio_eventloop *l,
                                                unsigned int backlog,
                                                cio_alloc_client alloc_client,
                                                cio_free_client free_client,
                                                cio_server_socket_close_hook close_hook)
{
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->backlog = (int)backlog;
	ss->impl.loop = l;
	ss->close_hook = close_hook;
	ss->close = socket_close;
	ss->accept = socket_accept;
	ss->set_reuse_address = socket_set_reuse_address;
	ss->bind = socket_bind;
	return CIO_SUCCESS;
}

static enum cio_error cio_server_socket_init_fails(struct cio_server_socket *ss,
												   struct cio_eventloop *l,
												   unsigned int backlog,
												   cio_alloc_client alloc_client,
												   cio_free_client free_client,
												   cio_server_socket_close_hook close_hook)
{
	(void)ss;
	(void)l;
	(void)backlog;
	(void)alloc_client;
	(void)free_client;
	(void)close_hook;

	return CIO_INVALID_ARGUMENT;
}

static void close_server_socket(struct cio_server_socket *ss)
{
	if (ss->close_hook != NULL) {
		ss->close_hook(ss);
	}
}

static struct cio_io_stream *get_mem_io_stream(struct cio_socket *context)
{
	(void)context;
	return &ms.ios;
}

static struct cio_io_stream *get_null_io_stream(struct cio_socket *context)
{
	(void)context;
	return NULL;
}

static void free_dummy_client(struct cio_socket *socket)
{
	struct cio_http_client *client = cio_container_of(socket, struct cio_http_client, socket);
	free(client);
}

static struct cio_socket *alloc_dummy_client(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	memset(client, 0xaf, sizeof(*client));
	client->buffer_size = read_buffer_size;
	client->socket.get_io_stream = get_mem_io_stream;
	client->socket.close_hook = free_dummy_client;
	return &client->socket;
}

static struct cio_socket *alloc_dummy_client_no_buffer(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + 0);
	memset(client, 0xaf, sizeof(*client));
	client->buffer_size = 0;
	client->socket.get_io_stream = get_mem_io_stream;
	client->socket.close_hook = free_dummy_client;
	return &client->socket;
}

static struct cio_socket *alloc_dummy_client_no_iostream(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	memset(client, 0xaf, sizeof(*client));
	client->buffer_size = read_buffer_size;
	client->socket.get_io_stream = get_null_io_stream;
	client->socket.close_hook = free_dummy_client;
	return &client->socket;
}

static enum cio_http_cb_return callback_write_response(struct cio_http_client *c)
{
	static const char data[] = "Hello World!";
	struct cio_http_location_handler *handler = c->current_handler;
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_const_element_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	c->write_response(c, CIO_HTTP_STATUS_NOT_FOUND, &dh->wbh, NULL);
	return CIO_HTTP_CB_SUCCESS;
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

static enum cio_http_cb_return header_complete_write_response(struct cio_http_client *c)
{
	static const char data[] = "Hello World!";
	struct cio_http_location_handler *handler = c->current_handler;
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_const_element_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	c->write_response(c, CIO_HTTP_STATUS_OK, &dh->wbh, response_written_cb);
	return CIO_HTTP_CB_SUCCESS;
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

static void free_dummy_handler(struct cio_http_location_handler *handler)
{
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	free(dh);
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
		handler->handler.on_header_field = on_header_field;
		handler->handler.on_header_value = on_header_value;
		handler->handler.on_url = on_url;
		handler->handler.on_headers_complete = on_header_complete;
		handler->handler.on_body = on_body;
		handler->handler.on_message_complete = on_message_complete;
		handler->handler.on_schema = on_schema;
		handler->handler.on_host = on_host;
		handler->handler.on_port = on_port;
		handler->handler.on_path = on_path;
		handler->handler.on_query = on_query;
		handler->handler.on_fragment = on_fragment;
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

static struct cio_http_location_handler *alloc_failing_handler(const void *config)
{
	(void)config;
	return NULL;
}

static struct dummy_handler handler_with_no_free = {
	.handler = { .free = NULL}
};

static struct cio_http_location_handler *alloc_handler_with_no_free(const void *config)
{
	(void)config;

	return &handler_with_no_free.handler;
}

static enum cio_error accept_save_handler(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	ss->handler = handler;
	ss->handler_context = handler_context;
	return CIO_SUCCESS;
}

static enum cio_error read_some_max(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *context)
{
	struct memory_stream *memory_stream = cio_container_of(ios, struct memory_stream, ios);
	size_t len = MIN(cio_read_buffer_size(buffer), memory_stream->size - memory_stream->read_pos);
	memcpy(buffer->data, &((uint8_t *)memory_stream->mem)[memory_stream->read_pos], len);
	memory_stream->read_pos += len;
	buffer->add_ptr += len;
	buffer->bytes_transferred = len;
	if (len > 0) {
		handler(ios, context, CIO_SUCCESS, buffer);
	}

	return CIO_SUCCESS;
}

static enum cio_error read_some_close(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *context)
{
	struct memory_stream *memory_stream = cio_container_of(ios, struct memory_stream, ios);
	size_t len = MIN(cio_read_buffer_size(buffer), memory_stream->size - memory_stream->read_pos);
	memcpy(buffer->data, &((uint8_t *)memory_stream->mem)[memory_stream->read_pos], len);
	memory_stream->read_pos += len;
	buffer->add_ptr += len;
	buffer->bytes_transferred = len;
	if (len == 0) {
		handler(ios, context, CIO_EOF, buffer);
	} else {
		handler(ios, context, CIO_SUCCESS, buffer);
	}

	return CIO_SUCCESS;
}

static enum cio_error read_some_error(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *context)
{
	(void)ios;
	(void)buffer;
	(void)handler;
	(void)context;
	return CIO_BAD_FILE_DESCRIPTOR;
}

static enum cio_error write_all(struct cio_io_stream *ios, const struct cio_write_buffer *buf, cio_io_stream_write_handler handler, void *handler_context)
{
	struct memory_stream *memory_stream = cio_container_of(ios, struct memory_stream, ios);

	size_t bytes_transferred = 0;
	size_t buffer_len = cio_write_buffer_get_number_of_elements(buf);
	const struct cio_write_buffer *data_buf = buf;

	for (unsigned int i = 0; i < buffer_len; i++) {
		data_buf = data_buf->next;
		memcpy(&memory_stream->write_buffer[memory_stream->write_pos], data_buf->data.element.const_data, data_buf->data.element.length);
		memory_stream->write_pos += data_buf->data.element.length;
		bytes_transferred += data_buf->data.element.length;
	}

	handler(ios, handler_context, buf, CIO_SUCCESS, bytes_transferred);
	return CIO_SUCCESS;
}

static enum cio_error write_error(struct cio_io_stream *ios, const struct cio_write_buffer *buf, cio_io_stream_write_handler handler, void *handler_context)
{
	(void)ios;
	(void)buf;
	(void)handler;
	(void)handler_context;
	return CIO_BAD_FILE_DESCRIPTOR;
}

static enum cio_error mem_close(struct cio_io_stream *io_stream)
{
	(void)io_stream;
	free_dummy_client(ms.socket);

	free(ms.mem);
	client_socket_close();
	return CIO_SUCCESS;
}

static void memory_stream_init(struct memory_stream *stream, const char *fill_pattern, struct cio_socket *s)
{
	stream->socket = s;
	stream->write_pos = 0;
	stream->read_pos = 0;
	stream->size = strlen(fill_pattern);
	stream->ios.read_some = read_some_max;
	stream->ios.write_some = write_all;
	stream->ios.close = mem_close;
	stream->mem = malloc(stream->size + 1);
	memset(stream->mem, 0x00, stream->size + 1);
	memcpy(stream->mem, fill_pattern, stream->size);
}

static http_parser response_parser;
static http_parser_settings response_parser_settings;

static void check_http_response(struct memory_stream *stream, int status_code)
{
	size_t nparsed = http_parser_execute(&response_parser, &response_parser_settings, (const char *)stream->write_buffer, stream->write_pos);
	TEST_ASSERT_EQUAL_MESSAGE(stream->write_pos, nparsed, "Not a valid http response!");
	TEST_ASSERT_EQUAL_MESSAGE(status_code, response_parser.status_code, "http response status code not correct!");
}

static void fire_keepalive_timeout(struct cio_socket *s)
{
	struct cio_http_client *client = cio_container_of(s, struct cio_http_client, socket);
	client->http_private.request_timer.handler(&client->http_private.request_timer, client->http_private.request_timer.handler_context, CIO_SUCCESS);
}

void setUp(void)
{
	FFF_RESET_HISTORY();
	RESET_FAKE(cio_server_socket_init);
	RESET_FAKE(socket_set_reuse_address);
	RESET_FAKE(socket_accept);
	RESET_FAKE(socket_bind);
	RESET_FAKE(serve_error);
	RESET_FAKE(socket_close);
	RESET_FAKE(http_close_hook);

	RESET_FAKE(on_schema);
	RESET_FAKE(on_host);
	RESET_FAKE(on_port);
	RESET_FAKE(on_path);
	RESET_FAKE(on_query);
	RESET_FAKE(on_fragment);
	RESET_FAKE(on_url);
	RESET_FAKE(on_header_field);
	RESET_FAKE(on_header_value);
	RESET_FAKE(on_header_complete);
	RESET_FAKE(on_body);
	RESET_FAKE(on_message_complete);

	RESET_FAKE(timer_expires_from_now);
	RESET_FAKE(timer_cancel);
	RESET_FAKE(timer_close);
	RESET_FAKE(cio_timer_init);

	SET_CUSTOM_FAKE_SEQ(cio_timer_init, NULL, 0);
	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
	timer_cancel_fake.custom_fake = cancel_timer;
	timer_expires_from_now_fake.custom_fake = expires;

	RESET_FAKE(client_socket_close);
	RESET_FAKE(location_handler_called);
	RESET_FAKE(sub_location_handler_called);

	RESET_FAKE(response_written_cb);

	http_parser_settings_init(&response_parser_settings);
	http_parser_init(&response_parser, HTTP_RESPONSE);

	socket_close_fake.custom_fake = close_server_socket;
	socket_accept_fake.custom_fake = accept_save_handler;
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
}

void tearDown(void)
{
}

static void test_server_init(void)
{
	struct server_init_arguments {
		struct cio_http_server *server;
		uint16_t port;
		struct cio_eventloop *loop;
		void (*serve_error)(struct cio_http_server *server, const char *reason);
		uint64_t header_read_timeout;
		uint64_t body_read_timeout;
		uint64_t response_timeout;
		struct cio_socket *(*alloc_client)(void);
		void (*free_client)(struct cio_socket *socket);
		enum cio_error (*server_socket_init)(struct cio_server_socket *ss, struct cio_eventloop *loop, unsigned int backlog, cio_alloc_client alloc_client, cio_free_client free_client, cio_server_socket_close_hook close_hook);
		enum cio_error expected_result;
	};

	struct cio_http_server server;
	struct server_init_arguments server_init_arguments[] = {
		{.server = &server, .port = 8080, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_SUCCESS},
		{.server = NULL, .port = 8080, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
		{.server = &server, .port = 0, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
		{.server = &server, .port = 8080, .loop = NULL, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
		{.server = &server, .port = 8080, .loop = &loop, .serve_error = NULL, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_SUCCESS},
		{.server = &server, .port = 8080, .loop = &loop, .serve_error = serve_error, .header_read_timeout = 0, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
		{.server = &server, .port = 8080, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = 0, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
		{.server = &server, .port = 8080, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = 0, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
		{.server = &server, .port = 8080, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = NULL, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
		{.server = &server, .port = 8080, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = NULL, .server_socket_init = cio_server_socket_init_ok, .expected_result = CIO_INVALID_ARGUMENT},
		{.server = &server, .port = 8080, .loop = &loop, .serve_error = serve_error, .header_read_timeout = header_read_timeout, .body_read_timeout = body_read_timeout, .response_timeout = response_timeout, .alloc_client = alloc_dummy_client, .free_client = free_dummy_client, .server_socket_init = cio_server_socket_init_fails, .expected_result = CIO_INVALID_ARGUMENT},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(server_init_arguments); i++) {
		struct server_init_arguments args = server_init_arguments[i];
		cio_server_socket_init_fake.custom_fake = args.server_socket_init;
		enum cio_error err = cio_http_server_init(args.server, args.port, args.loop, args.serve_error, args.header_read_timeout, args.body_read_timeout, args.response_timeout, args.alloc_client, args.free_client);
		TEST_ASSERT_EQUAL_MESSAGE(args.expected_result, err, "Initialization failed!");

		setUp();
	}
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
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");
		err = server.shutdown(&server, args.close_hook);
		TEST_ASSERT_EQUAL_MESSAGE(args.close_hook_call_count, http_close_hook_fake.call_count, "http close hook was not called correctly");
		TEST_ASSERT_EQUAL_MESSAGE(args.expected_result, err, "Server shutdown failed!");

		setUp();
	}
}

static void test_register_request_target(void)
{
	struct register_request_target_args {
		struct cio_http_server *server;
		struct cio_http_location *target;
		enum cio_error expected_result;
	};

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
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
		err = server.register_location(arg.server, arg.target);
		TEST_ASSERT_EQUAL_MESSAGE(arg.expected_result, err, "Register request target not handled correctly!");

		setUp();
	}
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
		{.location = "/foo", .sub_location = "/foo/bar",.request_target = "/foo", .expected_response = 200, .location_call_count = 1, .sub_location_call_count = 0},
		{.location = "/foo", .sub_location = "/foo/bar",.request_target = "/foo/", .expected_response = 200, .location_call_count = 1, .sub_location_call_count = 0},
		{.location = "/foo", .sub_location = "/foo/bar",.request_target = "/foo/bar", .expected_response = 200, .location_call_count = 0, .sub_location_call_count = 1},
		{.location = "/foo", .sub_location = "/foo/bar",.request_target = "/foo2", .expected_response = 404, .location_call_count = 0, .sub_location_call_count = 0},
		{.location = "/foo/",.sub_location = "/foo/bar", .request_target = "/foo", .expected_response = 404, .location_call_count = 0, .sub_location_call_count = 0},
		{.location = "/foo/",.sub_location = "/foo/bar", .request_target = "/foo/", .expected_response = 200, .location_call_count = 1, .sub_location_call_count = 0},
		{.location = "/foo/",.sub_location = "/foo/bar", .request_target = "/foo/bar", .expected_response = 200, .location_call_count = 0, .sub_location_call_count = 1},
		{.location = "/foo/",.sub_location = "/foo/bar", .request_target = "/foo2", .expected_response = 404, .location_call_count = 0, .sub_location_call_count = 0},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(location_tests); i++) {
		on_header_complete_fake.custom_fake = header_complete_write_response;
		struct location_test location_test = location_tests[i];

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target1;
		uintptr_t loc_marker = 0;
		err = cio_http_location_init(&target1, location_test.location, (void *)loc_marker, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target1 initialization failed!");
		err = server.register_location(&server, &target1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target1 failed!");

		struct cio_http_location target2;
		uintptr_t sub_loc_marker = 1;
		err = cio_http_location_init(&target2, location_test.sub_location, (void *)sub_loc_marker, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target2 initialization failed!");
		err = server.register_location(&server, &target2);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target2 failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		char start_line[100];
		snprintf(start_line, sizeof(start_line) - 1, "GET %s HTTP/1.1\r\n" CRLF, location_test.request_target);

		memory_stream_init(&ms, start_line, s);

		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
		check_http_response(&ms, location_test.expected_response);
		// Because the response is written in on_headers_complete, on_message_complete will not be called
		TEST_ASSERT_EQUAL_MESSAGE(0, on_message_complete_fake.call_count, "on_message_complete was not called!");
		if (location_test.expected_response == 200) {
			TEST_ASSERT_EQUAL_MESSAGE(1, on_header_complete_fake.call_count, "on_header_complete was not called!");
			TEST_ASSERT_EQUAL_MESSAGE(1, response_written_cb_fake.call_count, "response_written callback was not called!");
		}

		TEST_ASSERT_EQUAL_MESSAGE(location_test.location_call_count, location_handler_called_fake.call_count, "location handler was not called correctly");
		TEST_ASSERT_EQUAL_MESSAGE(location_test.sub_location_call_count, sub_location_handler_called_fake.call_count, "sub_location handler was not called correctly");

		TEST_ASSERT_EQUAL_MESSAGE(0, client_socket_close_fake.call_count, "client socket was closed before keepalive timeout triggered!");
		fire_keepalive_timeout(s);
		TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "client socket was not closed after keepalive timeout triggered!");
		setUp();
	}
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
		{.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF "Content-Length: 0" CRLF CRLF, .expected_response = 200, .immediate_close = false},
		{.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF "Content-Length: 0" CRLF "Connection: keep-alive" CRLF CRLF, .expected_response = 200, .immediate_close = false},
		{.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF "Content-Length: 0" CRLF "Connection: close" CRLF CRLF, .expected_response = 200, .immediate_close = true},
		{.location = "/foo", .request = "GET /foo HTTP/1.0" CRLF "Content-Length: 0" CRLF CRLF, .expected_response = 200, .immediate_close = true},
		{.location = "/foo", .request = "GET /foo HTTP/1.0" CRLF "Content-Length: 0" CRLF "Connection: keep-alive" CRLF CRLF, .expected_response = 200, .immediate_close = false},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(keepalive_tests); i++) {
		on_header_complete_fake.custom_fake = header_complete_write_response;
		struct keepalive_test keepalive_test = keepalive_tests[i];

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, keepalive_test.location, NULL, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		memory_stream_init(&ms, keepalive_test.request, s);

		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
		check_http_response(&ms, keepalive_test.expected_response);

		if (!keepalive_test.immediate_close) {
			TEST_ASSERT_EQUAL_MESSAGE(0, client_socket_close_fake.call_count, "client socket was closed before keepalive timeout triggered!");
			fire_keepalive_timeout(s);
		}

		TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "client socket was not closed after keepalive timeout triggered!");
		setUp();
	}
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
		enum callback_sends_response who_sends_response;
		int expected_response;
	};

	struct tests tests[] = {
		{ .expected_response = 500, .who_sends_response = NO_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_SCHEMA_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_HOST_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_PORT_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_PATH_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_QUERY_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_FRAGMENT_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_URL_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_HEADER_FIELD_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_HEADER_VALUE_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_BODY_SENDS_RESPONSE},
		{ .expected_response = 200, .who_sends_response = ON_HEADER_COMPLETE_SENDS_RESPONSE},
		{ .expected_response = 404, .who_sends_response = ON_MESSAGE_COMPLETE_SENDS_RESPONSE},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct tests test = tests[i];

		struct request_test callbacks = {
			.on_scheme = on_schema, .on_host = on_host, .on_port = on_port, .on_path = on_path, .on_query = on_query, .on_fragment = on_fragment, .on_url = on_url,
			.on_header_field = on_header_field, .on_header_value = on_header_value, .on_header_complete = on_header_complete,
			.on_body = on_body, .on_message_complete = on_message_complete,
		};

		if (test.who_sends_response == ON_SCHEMA_SENDS_RESPONSE) on_schema_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_HOST_SENDS_RESPONSE) on_host_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_PORT_SENDS_RESPONSE) on_port_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_PATH_SENDS_RESPONSE) on_path_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_QUERY_SENDS_RESPONSE) on_query_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_FRAGMENT_SENDS_RESPONSE) on_fragment_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_URL_SENDS_RESPONSE) on_url_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_HEADER_FIELD_SENDS_RESPONSE) on_header_field_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_HEADER_VALUE_SENDS_RESPONSE) on_header_value_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_BODY_SENDS_RESPONSE) on_body_fake.custom_fake = data_callback_write_response;
		if (test.who_sends_response == ON_HEADER_COMPLETE_SENDS_RESPONSE) on_header_complete_fake.custom_fake = header_complete_write_response;
		if (test.who_sends_response == ON_MESSAGE_COMPLETE_SENDS_RESPONSE) on_message_complete_fake.custom_fake = callback_write_response;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", &callbacks, alloc_handler_for_callback_test);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		memory_stream_init(&ms, "GET http://172.19.1.1:8080/foo?search=qry#fraggy" CRLF "Content-Length: 5" CRLF CRLF "Hello", s);

		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		TEST_ASSERT_EQUAL_MESSAGE(test.expected_response == 500 ? 1 : 0, serve_error_fake.call_count, "Serve error callback was called!");

		check_http_response(&ms, test.expected_response);

		switch(test.who_sends_response) {
			case ON_MESSAGE_COMPLETE_SENDS_RESPONSE:
				TEST_ASSERT_EQUAL_MESSAGE(1, on_message_complete_fake.call_count, "on_message_complete was not called!");
				/* FALLTHRU */
			case ON_BODY_SENDS_RESPONSE:
				TEST_ASSERT_EQUAL_MESSAGE(1, on_body_fake.call_count, "on_body was not called!");
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

		setUp();
	}
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

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", &request_test, request_test.alloc_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		static const char *scheme = "http";
		static const char *host = "172.19.1.1";
		static const char *port = "8080";
		static const char *path = "/foo";
		static const char *query = "search=qry";
		static const char *fragment = "fraggy";

		char buffer[200];
		snprintf(buffer, sizeof(buffer) - 1, "GET %s://%s:%s%s?%s#%s HTTP/1.1" CRLF "Content-Length: 5" CRLF CRLF "Hello", scheme, host, port, path, query, fragment);

		memory_stream_init(&ms, buffer, s);

		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		TEST_ASSERT_EQUAL_MESSAGE(request_test.expected_response == 500 ? 1 : 0, serve_error_fake.call_count, "Serve error callback was called!");

		struct cio_http_client *client = cio_container_of(s, struct cio_http_client, socket);

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

		check_http_response(&ms, request_test.expected_response);

		if (request_test.expected_response == 200) {
			fire_keepalive_timeout(s);
		}

		setUp();
	}
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

		socket_set_reuse_address_fake.return_val = serve_test.reuse_addres_retval;
		socket_bind_fake.return_val = serve_test.bind_retval;
		socket_accept_fake.custom_fake = NULL;
		socket_accept_fake.return_val = serve_test.accept_retval;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		err = server.serve(&server);
		TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http did not fail!");
		TEST_ASSERT_EQUAL_MESSAGE(1, socket_close_fake.call_count, "Close was not called!");

		setUp();
	}
}

static void test_errors_in_accept(void)
{
	struct accept_test {
		struct cio_socket *(*alloc_client)(void);
		enum cio_error (*response_timer_init)(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook);
		enum cio_error (*request_timer_init)(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook);
		enum cio_error (*timer_expires)(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context);
		enum cio_error accept_error_parameter;
		const char *request;
	};

	static const struct accept_test accept_tests[] = {
		{
			.accept_error_parameter = CIO_INVALID_ARGUMENT,
			.alloc_client = alloc_dummy_client,
			.response_timer_init = cio_timer_init_ok,
			.request_timer_init = cio_timer_init_ok,
			.timer_expires = expires,
			.request = "GET /foo HTTP/1.1" CRLF CRLF
		},
		{
			.accept_error_parameter = CIO_SUCCESS,
			.alloc_client = alloc_dummy_client_no_buffer,
			.response_timer_init = cio_timer_init_ok,
			.request_timer_init = cio_timer_init_ok,
			.timer_expires = expires,
			.request = "GET /foo HTTP/1.1" CRLF CRLF
		},
		{
			.accept_error_parameter = CIO_SUCCESS,
			.alloc_client = alloc_dummy_client_no_iostream,
			.response_timer_init = cio_timer_init_ok,
			.request_timer_init = cio_timer_init_ok,
			.timer_expires = expires,
			.request = "GET /foo HTTP/1.1" CRLF CRLF
		},
		{
			.accept_error_parameter = CIO_SUCCESS,
			.alloc_client = alloc_dummy_client,
			.response_timer_init = cio_timer_init_fails,
			.request_timer_init = cio_timer_init_ok,
			.timer_expires = expires,
			.request = "GET /foo HTTP/1.1" CRLF CRLF
		},
		{
			.accept_error_parameter = CIO_SUCCESS,
			.alloc_client = alloc_dummy_client,
			.response_timer_init = cio_timer_init_ok,
			.request_timer_init = cio_timer_init_fails,
			.timer_expires = expires,
			.request = "GET /foo HTTP/1.1" CRLF CRLF
		},
		{
			.accept_error_parameter = CIO_SUCCESS,
			.alloc_client = alloc_dummy_client,
			.response_timer_init = cio_timer_init_ok,
			.request_timer_init = cio_timer_init_ok,
			.timer_expires = expires_error,
			.request = "GET /foo HTTP/1.1" CRLF CRLF
		},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(accept_tests); i++) {
		struct accept_test accept_test = accept_tests[i];

		enum cio_error (*timer_init_fakes[])(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook) = {
        	accept_test.response_timer_init,
        	accept_test.request_timer_init
    	};

    	cio_timer_init_fake.custom_fake = NULL;
    	SET_CUSTOM_FAKE_SEQ(cio_timer_init, timer_init_fakes, ARRAY_SIZE(timer_init_fakes));

    	timer_expires_from_now_fake.custom_fake = accept_test.timer_expires;


		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, accept_test.alloc_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();
		memory_stream_init(&ms, accept_test.request, s);

		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, accept_test.accept_error_parameter, s);

		TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
		if (accept_test.alloc_client != alloc_dummy_client_no_iostream) {
			// if there is no stream, no socket could be closed
			TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "client socket was not closed if error in accept occured!");
		}

		if (accept_test.alloc_client == alloc_dummy_client_no_iostream) {
			// To prevent memory leaks allocated by memory_stream_init, we havve to free the memory in the test.
			free(ms.mem);
		}

		setUp();
	}
}

static void test_parse_errors(void)
{
	struct parse_test {
		enum cio_error (*read_some)(struct cio_io_stream *io_stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *handler_context);
		enum cio_error (*write_some)(struct cio_io_stream *io_stream, const struct cio_write_buffer *buf, cio_io_stream_write_handler handler, void *handler_context);
		const char *request;
		bool simulate_write_error;
		int expected_response;
	};

	static struct parse_test parse_tests[] = {
		{.read_some = read_some_error, .write_some = write_all, .request = "GET /foo HTTP/1.1" CRLF CRLF, .expected_response = 500},
		{.read_some = read_some_max, .write_some = write_all, .request = "GT /foo HTTP/1.1" CRLF CRLF, .expected_response = 400},
		{.read_some = read_some_max, .write_some = write_error, .request = "GT /foo HTTP/1.1" CRLF CRLF},
		{.read_some = read_some_max, .write_some = write_all, .request = "GET http://ww%.google.de/ HTTP/1.1" CRLF CRLF, .expected_response = 400},
		{.read_some = read_some_max, .write_some = write_all, .request = "CONNECT www.google.de:80 HTTP/1.1" CRLF CRLF, .expected_response = 400},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(parse_tests); i++) {
		struct parse_test parse_test = parse_tests[i];

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		memory_stream_init(&ms, parse_test.request, s);
		ms.ios.read_some = parse_test.read_some;
		ms.ios.write_some = parse_test.write_some;

		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);

		if (!parse_test.simulate_write_error) {
			check_http_response(&ms, parse_test.expected_response);
		}

		if (parse_test.expected_response == 500) {
			TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
		}

		setUp();
	}
}

static void test_error_without_error_callback(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, NULL, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	memory_stream_init(&ms, "GET /foo HTTP/1.1" CRLF CRLF, s);
	ms.ios.read_some = read_some_error;
	ms.ios.write_some = write_all;

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);

	check_http_response(&ms, 500);

	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was not called!");
}

static void test_client_close_while_reading(void)
{
	struct cio_http_server server;
	on_header_complete_fake.custom_fake = header_complete_write_response;

	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	memory_stream_init(&ms, "GET /foo HTTP/1.1" CRLF CRLF, s);
	ms.ios.read_some = read_some_close;

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 200);

	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "client socket was not closed after keepalive timeout triggered!");
}

static void test_connection_upgrade(void)
{
	struct cio_http_server server;
	on_header_complete_fake.custom_fake = header_complete_write_response;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_upgrade_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	memory_stream_init(&ms, "GET /foo HTTP/1.1" CRLF "Upgrade: websocket" CRLF "Connection: Upgrade" CRLF CRLF, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 101);
	fire_keepalive_timeout(s);
}

static void test_timer_cancel_errors(void)
{
	enum cio_error (*timer_cancel_fakes[3])(struct cio_timer *timer);

	for (unsigned int i = 0; i < ARRAY_SIZE(timer_cancel_fakes); i++) {
		timer_cancel_fakes[0] = cancel_timer;
		timer_cancel_fakes[1] = cancel_timer;
		timer_cancel_fakes[2] = cancel_timer;

		timer_cancel_fakes[i] = cancel_timer_error;

		timer_cancel_fake.custom_fake = NULL;
		SET_CUSTOM_FAKE_SEQ(timer_cancel, timer_cancel_fakes, ARRAY_SIZE(timer_cancel_fakes));

		on_header_complete_fake.custom_fake = header_complete_write_response;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		memory_stream_init(&ms, "GET /foo HTTP/1.1" CRLF CRLF, s);

		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was called!");

		TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "client socket was not closed after keepalive timeout triggered!");

		setUp();
	}
}

static void test_timer_expires_errors(void)
{
	enum cio_error (*timer_expires_fakes[4])(struct cio_timer *timer, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context);

	for (unsigned int i = 0; i < ARRAY_SIZE(timer_expires_fakes); i++) {
		timer_expires_fakes[0] = expires;
		timer_expires_fakes[1] = expires;
		timer_expires_fakes[2] = expires;
		timer_expires_fakes[3] = expires;

		timer_expires_fakes[i] = expires_error;

		timer_expires_from_now_fake.custom_fake = NULL;
		SET_CUSTOM_FAKE_SEQ(timer_expires_from_now, timer_expires_fakes, ARRAY_SIZE(timer_expires_fakes));

		on_header_complete_fake.custom_fake = header_complete_write_response;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");
		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		memory_stream_init(&ms, "GET /foo HTTP/1.1" CRLF CRLF, s);

		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		TEST_ASSERT_MESSAGE(serve_error_fake.call_count > 0, "Serve error callback was called!");

		TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "client socket was not closed after keepalive timeout triggered!");

		setUp();
	}
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_server_init);
	RUN_TEST(test_shutdown);
	RUN_TEST(test_register_request_target);
	RUN_TEST(test_serve_locations);
	RUN_TEST(test_keepalive_handling);
	RUN_TEST(test_url_callbacks);
	RUN_TEST(test_callbacks_after_response_sent);
	RUN_TEST(test_errors_in_serve);
	RUN_TEST(test_errors_in_accept);
	RUN_TEST(test_parse_errors);
	RUN_TEST(test_error_without_error_callback);
	RUN_TEST(test_client_close_while_reading);
	RUN_TEST(test_connection_upgrade);
	RUN_TEST(test_timer_cancel_errors);
	RUN_TEST(test_timer_expires_errors);
	return UNITY_END();
}
