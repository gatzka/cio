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
#include "cio_http_request_handler.h"
#include "cio_http_server.h"
#include "cio_server_socket.h"
#include "cio_timer.h"
#include "cio_write_buffer.h"

#include "http-parser/http_parser.h"

#undef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#undef container_of
#define container_of(ptr, type, member) ( \
    (void *)((char *)ptr - offsetof(type, member)))

#define HTTP_GET "GET"
#define HTTP_POST "POST"
#define BODY "HelloWorld!"
#define BODY1 "foobar!"
#define REQUEST_TARGET "/foo/"
#define REQUEST_TARGET_SUB "/foo/bar"
#define SCHEME "https"
#define HOST "www.example.com"
#define PORT "8080"
#define QUERY "p1=A&p2=B"
#define FRAGMENT "ressource"
#define HTTP_11 "HTTP/1.1"
#define KEEP_ALIVE_FIELD "Connection"
#define KEEP_ALIVE_VALUE "keep-alive"
#define DNT_FIELD "DNT"
#define DNT_VALUE "1"
#define CRLF "\r\n"

static const uint64_t read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

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

static enum cio_error bs_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler, void *)

static enum cio_error cancel_timer(struct cio_timer *t)
{
	t->handler(t, t->handler_context, cio_operation_aborted);
	return cio_success;
}

static void expires(struct cio_timer *t, uint64_t timeout_ns, timer_handler handler, void *handler_context)
{
	(void)timeout_ns;
	t->handler = handler;
	t->handler_context = handler_context;
}

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *loop, cio_timer_close_hook hook)
{
	(void)loop;
	timer->cancel = timer_cancel;
	timer->close = timer_close;
	timer->close_hook = hook;
	timer->expires_from_now = timer_expires_from_now;
	return cio_success;
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

static enum cio_http_cb_return header_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, header_complete, struct cio_http_client *)

static enum cio_http_cb_return message_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, message_complete, struct cio_http_client *)

static enum cio_http_cb_return header_complete_sub(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, header_complete_sub, struct cio_http_client *)

static enum cio_http_cb_return on_header_field(struct cio_http_client *, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_header_field, struct cio_http_client *, const char *, size_t)

static enum cio_http_cb_return on_header_value(struct cio_http_client *c, const char *, size_t);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_header_value, struct cio_http_client *, const char *, size_t)

static enum cio_http_cb_return on_body(struct cio_http_client *c, const char *, size_t);
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

static enum cio_http_cb_return on_url(struct cio_http_client *c, const char *at, size_t length);
FAKE_VALUE_FUNC(enum cio_http_cb_return, on_url, struct cio_http_client *, const char *, size_t)

static struct cio_io_stream *get_io_stream(struct cio_socket *context);
FAKE_VALUE_FUNC(struct cio_io_stream *, get_io_stream, struct cio_socket *)

static enum cio_error bs_read_until(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_read_until, struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *)

static enum cio_error bs_read(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_read, struct cio_buffered_stream *, struct cio_read_buffer *, cio_buffered_stream_read_handler, void *)

static void free_dummy_client(struct cio_socket *socket)
{
	struct cio_http_client *client = container_of(socket, struct cio_http_client, socket);
	free(client);
}

static struct cio_socket *alloc_dummy_client(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	memset(client, 0xaf, sizeof(*client));
	client->socket.get_io_stream = get_io_stream;
	client->socket.close_hook = free_dummy_client;
	return &client->socket;
}

static void free_dummy_handler(struct cio_http_request_handler *handler)
{
	struct dummy_handler *dh = container_of(handler, struct dummy_handler, handler);
	free(dh);
}

static enum cio_http_cb_return header_complete_write_response(struct cio_http_client *c)
{
	static const char data[] = "Hello World!";
	struct cio_http_request_handler *handler = c->handler;
	struct dummy_handler *dh = container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	c->write_response(c, &dh->wbh);
	return cio_http_cb_success;
}

static enum cio_http_cb_return header_complete_close_client(struct cio_http_client *c)
{
	c->close(c);
	return cio_http_cb_success;
}

static enum cio_http_cb_return message_complete_write_header(struct cio_http_client *c)
{
	c->write_header(c, cio_http_status_ok);
	return cio_http_cb_success;
}

static struct cio_http_request_handler *alloc_dummy_handler_msg_complete_only(const void *config)
{
	(void)config;
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_http_request_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		handler->handler.on_message_complete = message_complete;
		return &handler->handler;
	}
}

static struct cio_http_request_handler *alloc_dummy_handler(const void *config)
{
	(void)config;
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_http_request_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		handler->handler.on_header_field = on_header_field;
		handler->handler.on_header_value = on_header_value;
		handler->handler.on_url = on_url;
		handler->handler.on_headers_complete = header_complete;
		handler->handler.on_message_complete = message_complete;
		handler->handler.on_body = on_body;
		return &handler->handler;
	}
}

static struct cio_http_request_handler *alloc_dummy_handler_url_callbacks(const void *config)
{
	(void)config;
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_http_request_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		handler->handler.on_headers_complete = header_complete;
		handler->handler.on_message_complete = message_complete;
		handler->handler.on_schema = on_schema;
		handler->handler.on_host = on_host;
		handler->handler.on_port = on_port;
		handler->handler.on_path = on_path;
		handler->handler.on_query = on_query;
		handler->handler.on_fragment = on_fragment;
		return &handler->handler;
	}
}

static struct cio_http_request_handler *alloc_dummy_handler_sub(const void *config)
{
	(void)config;
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_http_request_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		handler->handler.free = free_dummy_handler;
		handler->handler.on_header_field = on_header_field;
		handler->handler.on_header_value = on_header_value;
		handler->handler.on_url = on_url;
		handler->handler.on_headers_complete = header_complete_sub;
		return &handler->handler;
	}
}

static enum cio_error accept_save_handler(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	ss->handler = handler;
	ss->handler_context = handler_context;
	return cio_success;
}

static const char **request_lines;
static size_t num_of_request_lines;
static unsigned int current_line;

static void init_request(const char **request, size_t lines)
{
	request_lines = request;
	num_of_request_lines = lines;
}

static enum cio_error bs_read_internal(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (current_line >= num_of_request_lines) {
		buffer->bytes_transferred = 0;
	} else {
		const char *line = request_lines[current_line];
		size_t length = strlen(line);
		memcpy(buffer->add_ptr, line, length);
		buffer->bytes_transferred = length;
		buffer->fetch_ptr = buffer->add_ptr + length;
		current_line++;
	}

	handler(bs, handler_context, cio_success, buffer);
	return cio_success;
}

static enum cio_error bs_read_until_ok(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)delim;

	return bs_read_internal(bs, buffer, handler, handler_context);
}

static enum cio_error bs_read_ok(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, cio_buffered_stream_read_handler handler, void *handler_context)
{
	return bs_read_internal(bs, buffer, handler, handler_context);
}

static enum cio_error bs_read_until_error(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)delim;

	handler(bs, handler_context, cio_invalid_argument, buffer);
	return cio_success;
}

static void close_client(struct cio_http_client *client)
{
	free_dummy_client(&client->socket);
}

static enum cio_error bs_close(struct cio_buffered_stream *bs)
{
	struct cio_http_client *client = container_of(bs, struct cio_http_client, bs);
	close_client(client);
	return cio_success;
}

static uint8_t write_buffer[1000];
static size_t write_pos;

static enum cio_error bs_write_all(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	size_t buffer_len = buf->data.q_len;
	const struct cio_write_buffer *data_buf = buf;

	for (unsigned int i = 0; i < buffer_len; i++) {
		data_buf = data_buf->next;
		memcpy(&write_buffer[write_pos], data_buf->data.element.data, data_buf->data.element.length);
		write_pos += data_buf->data.element.length;
	}

	handler(bs, handler_context, buf, cio_success);
	return cio_success;
}

static struct cio_buffered_stream *write_later_bs;
static struct cio_write_buffer *write_later_buf;
static cio_buffered_stream_write_handler write_later_handler;
static void *write_later_handler_context;

static enum cio_error bs_write_later(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	write_later_bs = bs;
	write_later_buf = buf;
	write_later_handler = handler;
	write_later_handler_context = handler_context;

	return cio_success;
}

enum cio_error cio_buffered_stream_init(struct cio_buffered_stream *bs,
                                        struct cio_io_stream *stream)
{
	(void)stream;
	bs->read_until = bs_read_until;
	bs->read = bs_read;
	bs->write = bs_write;
	bs->close = bs_close;
	return cio_success;
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

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(socket_set_reuse_address);
	RESET_FAKE(socket_accept);
	RESET_FAKE(socket_bind);
	RESET_FAKE(serve_error);
	RESET_FAKE(socket_close);
	RESET_FAKE(header_complete);
	RESET_FAKE(message_complete);
	RESET_FAKE(header_complete_sub);
	RESET_FAKE(on_url);
	RESET_FAKE(on_header_field);
	RESET_FAKE(on_header_value);
	RESET_FAKE(timer_expires_from_now);
	RESET_FAKE(timer_cancel);
	RESET_FAKE(bs_read_until);
	RESET_FAKE(bs_read);

	http_parser_settings_init(&parser_settings);
	http_parser_init(&parser, HTTP_RESPONSE);

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
	timer_cancel_fake.custom_fake = cancel_timer;
	timer_expires_from_now_fake.custom_fake = expires;

	bs_read_until_fake.custom_fake = bs_read_until_ok;
	bs_read_fake.custom_fake = bs_read_ok;
	current_line = 0;
	memset(write_buffer, 0xaf, sizeof(write_buffer));
	write_pos = 0;

	bs_write_fake.custom_fake = bs_write_all;
}

static void test_serve_first_line_fails(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_write_response;

	enum cio_error (*read_until_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *) = {
	    bs_read_until_error,
	    bs_read_until_ok,
	};

	bs_read_until_fake.custom_fake = NULL;
	SET_CUSTOM_FAKE_SEQ(bs_read_until, read_until_fakes, ARRAY_SIZE(read_until_fakes));

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    HTTP_GET " " REQUEST_TARGET " " HTTP_11 CRLF,
	    KEEP_ALIVE_FIELD ": " KEEP_ALIVE_VALUE CRLF,
	    DNT_FIELD ": " DNT_VALUE CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(500);
}

static void test_serve_first_line_fails_write_blocks(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_write_response;
	bs_write_fake.custom_fake = bs_write_later;

	enum cio_error (*read_until_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *) = {
	    bs_read_until_error,
	    bs_read_until_ok,
	};

	bs_read_until_fake.custom_fake = NULL;
	SET_CUSTOM_FAKE_SEQ(bs_read_until, read_until_fakes, ARRAY_SIZE(read_until_fakes));

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    HTTP_GET " " REQUEST_TARGET " " HTTP_11 CRLF,
	    KEEP_ALIVE_FIELD ": " KEEP_ALIVE_VALUE CRLF,
	    DNT_FIELD ": " DNT_VALUE CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);

	bs_write_all(write_later_bs, write_later_buf, write_later_handler, write_later_handler_context);

	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(500);
}

static void test_serve_second_line_fails(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_write_response;

	enum cio_error (*read_until_fakes[])(struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *) = {
	    bs_read_until_ok,
	    bs_read_until_error};

	bs_read_until_fake.custom_fake = NULL;
	SET_CUSTOM_FAKE_SEQ(bs_read_until, read_until_fakes, ARRAY_SIZE(read_until_fakes));

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    HTTP_GET " " REQUEST_TARGET " " HTTP_11 CRLF,
	    KEEP_ALIVE_FIELD ": " KEEP_ALIVE_VALUE CRLF,
	    DNT_FIELD ": " DNT_VALUE CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(500);
}

struct location_test {
	const char *location;
	const char *request_target;
	int expected_response;
};

static void test_serve_locations(void)
{
	static struct location_test location_tests[] = {
	    {.location = "/foo", .request_target = "/foo", .expected_response = 200},
	    {.location = "/foo", .request_target = "/foo/", .expected_response = 200},
	    {.location = "/foo", .request_target = "/foo/bar", .expected_response = 200},
	    {.location = "/foo", .request_target = "/foo2", .expected_response = 404},
	    {.location = "/foo/", .request_target = "/foo", .expected_response = 404},
	    {.location = "/foo/", .request_target = "/foo/", .expected_response = 200},
	    {.location = "/foo/", .request_target = "/foo/bar", .expected_response = 200},
	    {.location = "/foo/", .request_target = "/foo2", .expected_response = 404},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(location_tests); i++) {
		struct location_test location_test = location_tests[i];

		cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
		socket_accept_fake.custom_fake = accept_save_handler;

		header_complete_fake.custom_fake = header_complete_write_response;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

		struct cio_http_server_location target;
		err = cio_http_server_location_init(&target, location_test.location, NULL, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		char start_line[100];
		snprintf(start_line, sizeof(start_line) - 1, "GET %s HTTP/1.1\r\n", location_test.request_target);

		const char *request[] = {
		    start_line,
		    CRLF};

		init_request(request, ARRAY_SIZE(request));
		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
		TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
		check_http_response(location_test.expected_response);
		if (location_test.expected_response == 200) {
			TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
			TEST_ASSERT_EQUAL_MESSAGE(1, message_complete_fake.call_count, "message_complete was not called!");
		}

		setUp();
	}
}

struct best_match_test {
	const char *location1;
	cio_http_alloc_handler location1_handler;
	const char *location2;
	cio_http_alloc_handler location2_handler;
	const char *request;
};

static void test_serve_locations_best_match(void)
{
	static struct best_match_test best_match_tests[] = {
	    {.location1 = REQUEST_TARGET, .location1_handler = alloc_dummy_handler, .location2 = REQUEST_TARGET_SUB, .location2_handler = alloc_dummy_handler_sub, .request = REQUEST_TARGET_SUB},
	    {.location1 = REQUEST_TARGET_SUB, .location1_handler = alloc_dummy_handler_sub, .location2 = REQUEST_TARGET, .location2_handler = alloc_dummy_handler, .request = REQUEST_TARGET_SUB},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(best_match_tests); i++) {
		struct best_match_test best_match_test = best_match_tests[i];

		cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
		socket_accept_fake.custom_fake = accept_save_handler;

		header_complete_fake.custom_fake = header_complete_write_response;
		header_complete_sub_fake.custom_fake = header_complete_write_response;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

		struct cio_http_server_location target;
		err = cio_http_server_location_init(&target, best_match_test.location1, NULL, best_match_test.location1_handler);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

		struct cio_http_server_location target_sub;
		err = cio_http_server_location_init(&target_sub, best_match_test.location2, NULL, best_match_test.location2_handler);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request sub target initialization failed!");

		err = server.register_location(&server, &target_sub);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request sub target failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		char start_line[100];
		snprintf(start_line, sizeof(start_line) - 1, "GET %s HTTP/1.1\r\n", best_match_test.request);

		const char *request[] = {
		    start_line,
		    CRLF};

		init_request(request, ARRAY_SIZE(request));
		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
		TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called!");
		TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_sub_fake.call_count, "header_complete_sub was not called!");
		TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
		check_http_response(200);

		setUp();
	}
}

static void test_serve_post_with_body(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;
	message_complete_fake.custom_fake = message_complete_write_header;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	char line[100];
	snprintf(line, sizeof(line) - 1, "Content-Length: %zu\r\n", strlen(BODY BODY1));

	const char *request[] = {
	    HTTP_POST " " REQUEST_TARGET " " HTTP_11 CRLF,
	    line,
	    CRLF,
	    BODY,
	    BODY1};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, message_complete_fake.call_count, "message_complete was called!");
	TEST_ASSERT_MESSAGE(on_body_fake.call_count > 0, "on_body was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(200);
}

static void test_serve_complete_url(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;
	message_complete_fake.custom_fake = message_complete_write_header;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET, NULL, alloc_dummy_handler_url_callbacks);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    HTTP_GET " " SCHEME "://" HOST ":" PORT REQUEST_TARGET "?" QUERY "#" FRAGMENT " " HTTP_11 CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, message_complete_fake.call_count, "message_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_schema_fake.call_count, "on_schema was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_host_fake.call_count, "on_host was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_port_fake.call_count, "on_port was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_path_fake.call_count, "on_path was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_query_fake.call_count, "on_query was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, on_fragment_fake.call_count, "on_fragment was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(200);
}

static void test_serve_msg_complete_only(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;
	message_complete_fake.custom_fake = message_complete_write_header;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET, NULL, alloc_dummy_handler_msg_complete_only);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    HTTP_GET " " REQUEST_TARGET " " HTTP_11 CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, message_complete_fake.call_count, "message_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(200);
}

static void test_serve_upgrade(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;
	header_complete_fake.custom_fake = header_complete_close_client;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET, NULL, alloc_dummy_handler_url_callbacks);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    HTTP_GET " " REQUEST_TARGET " " HTTP_11 CRLF,
	    "Upgrade: websocket" CRLF,
	    "Connection: Upgrade" CRLF,
	    KEEP_ALIVE_FIELD ": " KEEP_ALIVE_VALUE CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, timer_cancel_fake.call_count, "timer_cancel for read timeout was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
}

static void test_serve_upgrade_without_on_headers_complete(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;
	header_complete_fake.custom_fake = header_complete_close_client;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_server_location target;
	err = cio_http_server_location_init(&target, REQUEST_TARGET, NULL, alloc_dummy_handler_msg_complete_only);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    HTTP_GET " " REQUEST_TARGET " " HTTP_11 CRLF,
	    "Upgrade: websocket" CRLF,
	    "Connection: Upgrade" CRLF,
	    KEEP_ALIVE_FIELD ": " KEEP_ALIVE_VALUE CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, header_complete_fake.call_count, "header_complete was called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, timer_cancel_fake.call_count, "timer_cancel for read timeout was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(500);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_serve_first_line_fails);
	RUN_TEST(test_serve_first_line_fails_write_blocks);
	RUN_TEST(test_serve_second_line_fails);
	RUN_TEST(test_serve_locations);
	RUN_TEST(test_serve_locations_best_match);
	RUN_TEST(test_serve_post_with_body);
	RUN_TEST(test_serve_complete_url);
	RUN_TEST(test_serve_msg_complete_only);
	RUN_TEST(test_serve_upgrade);
	RUN_TEST(test_serve_upgrade_without_on_headers_complete);
	return UNITY_END();
}
