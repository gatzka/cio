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

#define HTTP_GET "GET"
#define HTTP_CONNECT "CONNECT"
#define REQUEST_TARGET_CONNECT "www.google.de:80"
#define REQUEST_TARGET1 "/foo"
#define ILLEGAL_REQUEST_TARGET "http://ww%.google.de/"
#define HTTP_11 "HTTP/1.1"
#define WRONG_HTTP_11 "HTTP}1.1"
#define KEEP_ALIVE_FIELD "Connection"
#define KEEP_ALIVE_VALUE "keep-alive"
#define DNT_FIELD "DNT"
#define DNT_VALUE "1"
#define CRLF "\r\n"






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


static enum cio_http_cb_return message_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, message_complete, struct cio_http_client *)
static enum cio_http_cb_return header_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, header_complete, struct cio_http_client *)
static enum cio_http_cb_return on_header_field(struct cio_http_client *, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_header_field, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_header_value(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_header_value, struct cio_http_client *, const char *, size_t)
static enum cio_http_cb_return on_url(struct cio_http_client *c, const char *at, size_t length);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_url, struct cio_http_client *, const char *, size_t)

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

/*
static void close_client(struct cio_http_client *client)
{
	if (cio_likely(client->current_handler != NULL)) {
		client->current_handler->free(client->current_handler);
	}

	client->http_private.request_timer.close(&client->http_private.request_timer);
	client->bs.close(&client->bs);
}

*/

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


/*

#define HISTORY_LENGTH 10
#define HISTORY_FIELD_LENGTH 10
static uint8_t header_field_history[HISTORY_LENGTH][HISTORY_FIELD_LENGTH];
static uint8_t header_value_history[HISTORY_LENGTH][HISTORY_FIELD_LENGTH];


static enum cio_http_cb_return on_header_field_capture(struct cio_http_client *client, const char *at, size_t length)
{
	(void)client;
	memcpy(header_field_history[on_header_field_fake.call_count - 1], at, length);
	return CIO_HTTP_CB_SUCCESS;
}

static enum cio_http_cb_return on_header_value_capture(struct cio_http_client *client, const char *at, size_t length)
{
	(void)client;
	memcpy(header_value_history[on_header_value_fake.call_count - 1], at, length);
	return CIO_HTTP_CB_SUCCESS;
}

*/
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

/*

static enum cio_http_cb_return header_complete_close(struct cio_http_client *c)
{
	c->close(c);
	return CIO_HTTP_CB_SUCCESS;
}

*/
static enum cio_http_cb_return header_complete_write_response(struct cio_http_client *c)
{
	static const char data[] = "Hello World!";
	struct cio_http_location_handler *handler = c->current_handler;
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_const_element_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	c->write_response(c, CIO_HTTP_STATUS_OK, &dh->wbh, NULL);
	return CIO_HTTP_CB_SUCCESS;
}

/*
static enum cio_http_cb_return header_complete_no_close(struct cio_http_client *c)
{
	(void)c;
	return CIO_HTTP_CB_SUCCESS;
}

static enum cio_http_cb_return on_url_close(struct cio_http_client *c, const char *at, size_t length)
{
	(void)at;
	(void)length;
	c->close(c);
	return CIO_HTTP_CB_SUCCESS;
}

*/

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
		handler->handler.on_headers_complete = header_complete;
		handler->handler.on_message_complete = message_complete;
		return &handler->handler;
	}
}

/*
static struct cio_http_location_handler *alloc_handler_no_callbacks(const void *config)
{
	(void)config;
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_http_location_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		return &handler->handler;
	}
}

static struct cio_http_location_handler *alloc_failing_handler(const void *config)
{
	(void)config;
	return NULL;
}
*/
static enum cio_error accept_save_handler(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	ss->handler = handler;
	ss->handler_context = handler_context;
	return CIO_SUCCESS;
}

/*


static enum cio_error read_blocks(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *context)
{
	(void)ios;
	(void)buffer;
	(void)handler;
	(void)context;
	return CIO_SUCCESS;
}

*/

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

	RESET_FAKE(message_complete);
	RESET_FAKE(header_complete);
	RESET_FAKE(on_url);
	RESET_FAKE(on_header_field);
	RESET_FAKE(on_header_value);

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

	http_parser_settings_init(&response_parser_settings);
	http_parser_init(&response_parser, HTTP_RESPONSE);

/*

	on_header_field_fake.custom_fake = on_header_field_capture;
	on_header_value_fake.custom_fake = on_header_value_capture;
	memset(header_field_history, 0xaf, sizeof(header_field_history[0][0]) * HISTORY_FIELD_LENGTH * HISTORY_LENGTH);
	memset(header_value_history, 0xaf, sizeof(header_value_history[0][0]) * HISTORY_FIELD_LENGTH * HISTORY_LENGTH);
*/

	socket_close_fake.custom_fake = close_server_socket;
	socket_accept_fake.custom_fake = accept_save_handler;
	header_complete_fake.custom_fake = header_complete_write_response;
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
		TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was not called!");
		if (location_test.expected_response == 200) {
			TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
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
		{.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF CRLF, .expected_response = 200, .immediate_close = false},
		{.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF "Connection: keep-alive" CRLF CRLF, .expected_response = 200, .immediate_close = false},
		{.location = "/foo", .request = "GET /foo HTTP/1.1" CRLF "Connection: close" CRLF CRLF, .expected_response = 200, .immediate_close = true},
		{.location = "/foo", .request = "GET /foo HTTP/1.0" CRLF CRLF, .expected_response = 200, .immediate_close = true},
		{.location = "/foo", .request = "GET /foo HTTP/1.0" CRLF "Connection: keep-alive" CRLF CRLF, .expected_response = 200, .immediate_close = false},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(keepalive_tests); i++) {
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

static void test_requests(void)
{

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
		{.read_some = read_some_max, .write_some = write_error, .request = "GT /foo HTTP/1.1" CRLF CRLF, },
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

static void test_client_close_while_reading(void)
{
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
	ms.ios.read_some = read_some_close;

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 200);

	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "client socket was not closed after keepalive timeout triggered!");
}
/*

static void test_serve_timeout(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_write_response;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);
	ms.ios.read_some = read_blocks;

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	struct cio_timer *timer = timer_expires_from_now_fake.arg0_val;
	cio_timer_handler handler = timer_expires_from_now_fake.arg2_val;
	void *context = timer_expires_from_now_fake.arg3_val;
	handler(timer, context, CIO_SUCCESS);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_correctly(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_write_response;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF "Connection: close" CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");

	check_http_response(&ms, 200);
}

static void test_serve_alloc_handler_with_no_callbacks(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_handler_no_callbacks);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");

	check_http_response(&ms, 500);
}

static void test_serve_alloc_handler_fails(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_failing_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");

	check_http_response(&ms, 500);
}

static void test_serve_correctly_with_header_fields(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_no_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] =
	    HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF
	        KEEP_ALIVE_FIELD ": " KEEP_ALIVE_VALUE CRLF
	            DNT_FIELD ": " DNT_VALUE CRLF
	                CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(2, on_header_field_fake.call_count, "on_header_field was not called twice!");
	TEST_ASSERT_EQUAL_MESSAGE(2, on_header_value_fake.call_count, "on_header_value was not called twice!");
	TEST_ASSERT_MESSAGE(memcmp(header_field_history[0], KEEP_ALIVE_FIELD, strlen(KEEP_ALIVE_FIELD)) == 0, "Header field is not correct!");
	TEST_ASSERT_MESSAGE(memcmp(header_value_history[0], KEEP_ALIVE_VALUE, strlen(KEEP_ALIVE_VALUE)) == 0, "Header value is not correct!");
	TEST_ASSERT_MESSAGE(memcmp(header_field_history[1], DNT_FIELD, strlen(DNT_FIELD)) == 0, "Header field is not correct!");
	TEST_ASSERT_MESSAGE(memcmp(header_value_history[1], DNT_VALUE, strlen(DNT_VALUE)) == 0, "Header value is not correct!");

	struct cio_http_client *client = cio_container_of(s, struct cio_http_client, socket);
	close_client(client);
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_with_wrong_header_fields(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_no_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] =
	    HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF
	        KEEP_ALIVE_FIELD ": " KEEP_ALIVE_VALUE CRLF
	            DNT_FIELD " " DNT_VALUE CRLF
	                CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_header_field_fake.call_count, "on_header_field was not called twice!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_header_value_fake.call_count, "on_header_field was not called twice!");
	TEST_ASSERT_MESSAGE(memcmp(header_field_history[0], KEEP_ALIVE_FIELD, strlen(KEEP_ALIVE_FIELD)) == 0, "Header field is not correct!");
	TEST_ASSERT_MESSAGE(memcmp(header_value_history[0], KEEP_ALIVE_VALUE, strlen(KEEP_ALIVE_VALUE)) == 0, "Header value is not correct!");

	check_http_response(&ms, 400);

	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_timer_init_fails(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;
	cio_timer_init_fake.custom_fake = cio_timer_init_fails;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called despite timer initialization failed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
}

static void test_serve_timer_init_fails_no_serve_error_cb(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;
	cio_timer_init_fake.custom_fake = cio_timer_init_fails;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, NULL, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called despite not given in initialization!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
}

static void test_serve_init_fails_reuse_address(void)
{
	socket_set_reuse_address_fake.return_val = CIO_INVALID_ARGUMENT;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http did not fail!");

	TEST_ASSERT_EQUAL_MESSAGE(1, socket_close_fake.call_count, "Close was not called!");
}

static void test_serve_init_fails_bind(void)
{
	socket_bind_fake.return_val = CIO_INVALID_ARGUMENT;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http did not fail!");

	TEST_ASSERT_EQUAL_MESSAGE(1, socket_close_fake.call_count, "Close was not called!");
}

static void test_serve_init_fails_accept(void)
{
	socket_accept_fake.return_val = CIO_INVALID_ARGUMENT;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http did not fail!");

	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_accept_fails(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_INVALID_ARGUMENT, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called despite accept error!");
	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
}

static void test_serve_accept_fails_no_error_callback(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, NULL, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_INVALID_ARGUMENT, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called despite accept error!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_illegal_start_line(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " WRONG_HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 400);
}

static void test_serve_connect_method(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_CONNECT " " REQUEST_TARGET_CONNECT " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 400);
}

static void test_serve_illegal_url(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " ILLEGAL_REQUEST_TARGET " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 400);
}

static void test_serve_correctly_on_url_close(void)
{
	socket_accept_fake.custom_fake = accept_save_handler;

	on_url_fake.custom_fake = on_url_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, on_url_fake.call_count, "on_url was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}
*/

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_server_init);
	RUN_TEST(test_shutdown);
	RUN_TEST(test_register_request_target);
	RUN_TEST(test_serve_locations);
	RUN_TEST(test_keepalive_handling);
	RUN_TEST(test_requests);
	RUN_TEST(test_errors_in_serve);
	RUN_TEST(test_errors_in_accept);
	RUN_TEST(test_parse_errors);
	RUN_TEST(test_client_close_while_reading);
/*
	RUN_TEST(test_serve_correctly);
	RUN_TEST(test_serve_timeout);
	RUN_TEST(test_serve_alloc_handler_with_no_callbacks);
	RUN_TEST(test_serve_alloc_handler_fails);
	RUN_TEST(test_serve_correctly_with_header_fields);
	RUN_TEST(test_serve_with_wrong_header_fields);
	RUN_TEST(test_serve_timer_init_fails);
	RUN_TEST(test_serve_timer_init_fails_no_serve_error_cb);
	RUN_TEST(test_serve_init_fails_reuse_address);
	RUN_TEST(test_serve_init_fails_bind);
	RUN_TEST(test_serve_init_fails_accept);
	RUN_TEST(test_serve_accept_fails);
	RUN_TEST(test_serve_accept_fails_no_error_callback);
	RUN_TEST(test_serve_illegal_start_line);
	RUN_TEST(test_serve_connect_method);
	RUN_TEST(test_serve_illegal_url);
	RUN_TEST(test_serve_correctly_on_url_close);
*/
	return UNITY_END();
}
