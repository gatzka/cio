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
#include "cio_http_server.h"
#include "cio_server_socket.h"
#include "cio_timer.h"
#include "cio_write_buffer.h"

#include "http-parser/http_parser.h"

#undef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#undef container_of
#define container_of(ptr, type, member) ( \
	(void *)((char *)ptr - offsetof(type, member)))

#define HTTP_GET "GET"
#define HTTP_CONNECT "CONNECT"
#define REQUEST_TARGET_CONNECT "www.google.de:80"
#define REQUEST_TARGET1 "/foo"
#define REQUEST_TARGET2 "/bar"
#define ILLEGAL_REQUEST_TARGET "http://ww%.google.de/"
#define HTTP_11 "HTTP/1.1"
#define WRONG_HTTP_11 "HTTP}1.1"
#define CRLF "\r\n"

static const uint64_t read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

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
	struct cio_http_request_handler handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
};

static const size_t read_buffer_size = 200;

DEFINE_FFF_GLOBALS

static void socket_close(struct cio_server_socket *context);
FAKE_VOID_FUNC(socket_close, struct cio_server_socket *)

static void serve_error(struct cio_http_server *server);
FAKE_VOID_FUNC(serve_error, struct cio_http_server *)

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


static enum cio_error timer_cancel(struct cio_timer *t);
FAKE_VALUE_FUNC(enum cio_error, timer_cancel, struct cio_timer *)

static void timer_close(struct cio_timer *t);
FAKE_VOID_FUNC(timer_close, struct cio_timer *)

static void timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, timer_handler handler, void *handler_context);
FAKE_VOID_FUNC(timer_expires_from_now, struct cio_timer *, uint64_t, timer_handler, void *)

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *loop, cio_timer_close_hook hook)
{
	(void)loop;
	timer->cancel = timer_cancel;
	timer->close = timer_close;
	timer->close_hook = hook;
	timer->expires_from_now = timer_expires_from_now;
	return cio_success;
}

static enum cio_error cio_timer_init_fails(struct cio_timer *timer, struct cio_eventloop *loop, cio_timer_close_hook hook)
{
	(void)loop;
	(void)timer;
	(void)hook;
	return cio_invalid_argument;
}

static enum cio_error cio_server_socket_init_ok(struct cio_server_socket *ss,
									  struct cio_eventloop *loop,
									  unsigned int backlog,
									  cio_alloc_client alloc_client,
									  cio_free_client free_client,
									  cio_server_socket_close_hook close_hook)
{
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->backlog = (int)backlog;
	ss->loop = loop;
	ss->close_hook = close_hook;
	ss->close = socket_close;
	ss->accept = socket_accept;
	ss->set_reuse_address = socket_set_reuse_address;
	ss->bind = socket_bind;
	return cio_success;
}

static void client_socket_close(void);
FAKE_VOID_FUNC0(client_socket_close)

static enum cio_error cio_server_socket_init_fails(struct cio_server_socket *ss,
									  struct cio_eventloop *loop,
									  unsigned int backlog,
									  cio_alloc_client alloc_client,
									  cio_free_client free_client,
									  cio_server_socket_close_hook close_hook)
{
	(void)ss;
	(void)loop;
	(void)backlog;
	(void)alloc_client;
	(void)free_client;
	(void)close_hook;

	return cio_not_enough_memory;
}

static enum cio_http_cb_return header_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, header_complete, struct cio_http_client *)


static enum cio_http_cb_return on_url(struct cio_http_client *c, const char *at, size_t length);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_url, struct cio_http_client *, const char *, size_t)

static struct cio_io_stream *get_mem_io_stream(struct cio_socket *context)
{
	(void)context;
	return &ms.ios;
}

static void free_dummy_client(struct cio_socket *socket)
{
	struct cio_http_client *client = container_of(socket, struct cio_http_client, socket);
	free(client);
}

static struct cio_socket *alloc_dummy_client(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	memset(client, 0xaf, sizeof(*client));
	client->socket.get_io_stream = get_mem_io_stream;
	client->socket.close_hook = free_dummy_client;
	return &client->socket;
}

static void free_dummy_handler(struct cio_http_request_handler *handler)
{
	struct dummy_handler *dh = container_of(handler, struct dummy_handler, handler);
	free(dh);
}

static enum cio_http_cb_return header_complete_close(struct cio_http_client *c)
{
	c->close(c);
	return cio_http_cb_success;
}

static enum cio_http_cb_return on_url_close(struct cio_http_client *c, const char *at, size_t length)
{
	(void)at;
	(void)length;
	c->close(c);
	return cio_http_cb_success;
}

static struct cio_http_request_handler *alloc_dummy_handler(const void *config)
{
	(void)config;
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		handler->handler.on_header_field = NULL;
		handler->handler.on_header_value = NULL;
		handler->handler.on_url = on_url;
		handler->handler.on_headers_complete = header_complete;
		return &handler->handler;
	}
}

static enum cio_error accept_save_handler(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	ss->handler = handler;
	ss->handler_context = handler_context;
	return cio_success;
}

static struct cio_eventloop loop;

static http_parser parser;
static http_parser_settings parser_settings;

static enum cio_error read_some_max(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *context)
{
	struct memory_stream *memory_stream = container_of(ios, struct memory_stream, ios);
	size_t len = MIN(cio_read_buffer_size(buffer), memory_stream->size - memory_stream->read_pos);
	memcpy(buffer->data, &((uint8_t *)memory_stream->mem)[memory_stream->read_pos], len);
	memory_stream->read_pos += len;
	buffer->add_ptr += len;
	buffer->bytes_transferred = len;
	handler(ios, context, cio_success, buffer);
	return cio_success;
}

static enum cio_error read_some_error(struct cio_io_stream *ios, struct cio_read_buffer *buffer, cio_io_stream_read_handler handler, void *context)
{
	handler(ios, context, cio_bad_address, buffer);
	return cio_success;
}

static enum cio_error mem_close(struct cio_io_stream *io_stream)
{
	(void)io_stream;
	free_dummy_client(ms.socket);

	free(ms.mem);
	client_socket_close();
	return cio_success;
}

static enum cio_error write_all(struct cio_io_stream *ios, const struct cio_write_buffer *buf, cio_io_stream_write_handler handler, void *handler_context)
{
	struct memory_stream *memory_stream = container_of(ios, struct memory_stream, ios);

	size_t bytes_transferred = 0;
	size_t buffer_len = buf->data.q_len;
	const struct cio_write_buffer *data_buf = buf;

	for (unsigned int i = 0; i < buffer_len; i++) {
		data_buf = data_buf->next;
		memcpy(&memory_stream->write_buffer[memory_stream->write_pos], data_buf->data.element.data, data_buf->data.element.length);
		memory_stream->write_pos += data_buf->data.element.length;
		bytes_transferred += data_buf->data.element.length;
	}

	handler(ios, handler_context, buf, cio_success, bytes_transferred);
	return cio_success;
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
	strncpy(ms.mem, fill_pattern, ms.size);
}

static void check_http_response(struct memory_stream *stream, int status_code)
{
	size_t nparsed = http_parser_execute(&parser, &parser_settings, (const char *)stream->write_buffer, stream->write_pos);
	TEST_ASSERT_EQUAL_MESSAGE(stream->write_pos, nparsed, "Not a valid http response!");
	TEST_ASSERT_EQUAL_MESSAGE(status_code, parser.status_code, "http response status code not correct!");
}

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(socket_set_reuse_address);
	RESET_FAKE(socket_accept);
	RESET_FAKE(socket_bind);
	RESET_FAKE(serve_error);
	RESET_FAKE(socket_close);
	RESET_FAKE(header_complete);
	RESET_FAKE(on_url);
	RESET_FAKE(client_socket_close);

	http_parser_settings_init(&parser_settings);
	http_parser_init(&parser, HTTP_RESPONSE);

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
}

static void test_server_init_correctly(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Initialization failed!");
}

static void test_server_init_no_server(void)
{
	enum cio_error err = cio_http_server_init(NULL, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_server_init_no_loop(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, NULL, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_server_init_no_alloc(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, NULL, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_server_init_no_free(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_server_init_no_timeout(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, 0, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization with 0 timeout did not fail!");
}

static void test_request_target_init_correctly(void)
{
	struct cio_http_uri_server_location target;
	enum cio_error err = cio_http_server_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Initialization failed!");
}

static void test_request_target_init_no_target(void)
{
	enum cio_error err = cio_http_server_location_init(NULL, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_request_target_init_no_request_target(void)
{
	struct cio_http_uri_server_location target;
	enum cio_error err = cio_http_server_location_init(&target, NULL, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_request_target_init_no_alloc_handler(void)
{
	struct cio_http_uri_server_location target;
	enum cio_error err = cio_http_server_location_init(&target, "/foo", NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_register_request_target_correctly(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");
}

static void test_register_request_target_no_server(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(NULL, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Register request target did not fail!");
}

static void test_register_request_target_no_target(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	err = server.register_location(&server, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Register request target did not fail!");
}

static void test_serve_correctly(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_timer_init_fails(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;
	cio_timer_init_fake.custom_fake = cio_timer_init_fails;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called despite timer initialization failed!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
}

static void test_serve_init_fails(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_fails;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(cio_success, err, "Serving http did not fail!");
}

static void test_serve_init_fails_reuse_address(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_set_reuse_address_fake.return_val = cio_invalid_argument;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(cio_success, err, "Serving http did not fail!");

	TEST_ASSERT_EQUAL_MESSAGE(1, socket_close_fake.call_count, "Close was not called!");
}

static void test_serve_init_fails_bind(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_bind_fake.return_val = cio_invalid_argument;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(cio_success, err, "Serving http did not fail!");

	TEST_ASSERT_EQUAL_MESSAGE(1, socket_close_fake.call_count, "Close was not called!");
}

static void test_serve_init_fails_accept(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.return_val = cio_invalid_argument;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(cio_success, err, "Serving http did not fail!");

	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_wrong_request_target(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET2, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	check_http_response(&ms, 404);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_accept_fails(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_not_enough_memory, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called despite accept error!");
	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
}

static void test_serve_accept_fails_no_error_callback(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, NULL, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_not_enough_memory, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called despite accept error!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_illegal_start_line(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " WRONG_HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 400);
}

static void test_serve_read_fails(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);
	ms.ios.read_some = read_some_error;

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 500);
}

static void test_serve_connect_method(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_CONNECT " " REQUEST_TARGET_CONNECT " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 400);
}

static void test_serve_illegal_url(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " ILLEGAL_REQUEST_TARGET " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(&ms, 400);
}

static void test_serve_correctly_on_url_close(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	on_url_fake.custom_fake = on_url_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_uri_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET1, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char request[] = HTTP_GET " " REQUEST_TARGET1 " " HTTP_11 CRLF CRLF;
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, on_url_fake.call_count, "on_url was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, client_socket_close_fake.call_count, "Client socket was not closed!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_server_init_correctly);
	RUN_TEST(test_server_init_no_server);
	RUN_TEST(test_server_init_no_loop);
	RUN_TEST(test_server_init_no_alloc);
	RUN_TEST(test_server_init_no_free);
	RUN_TEST(test_server_init_no_timeout);
	RUN_TEST(test_request_target_init_correctly);
	RUN_TEST(test_request_target_init_no_alloc_handler);
	RUN_TEST(test_request_target_init_no_request_target);
	RUN_TEST(test_request_target_init_no_target);
	RUN_TEST(test_register_request_target_correctly);
	RUN_TEST(test_register_request_target_no_server);
	RUN_TEST(test_register_request_target_no_target);
	RUN_TEST(test_serve_correctly);
	RUN_TEST(test_serve_timer_init_fails);
	RUN_TEST(test_serve_init_fails);
	RUN_TEST(test_serve_init_fails_reuse_address);
	RUN_TEST(test_serve_init_fails_bind);
	RUN_TEST(test_serve_init_fails_accept);
	RUN_TEST(test_serve_wrong_request_target);
	RUN_TEST(test_serve_accept_fails);
	RUN_TEST(test_serve_accept_fails_no_error_callback);
	RUN_TEST(test_serve_illegal_start_line);
	RUN_TEST(test_serve_read_fails);
	RUN_TEST(test_serve_connect_method);
	RUN_TEST(test_serve_illegal_url);
	RUN_TEST(test_serve_correctly_on_url_close);
	return UNITY_END();
}
