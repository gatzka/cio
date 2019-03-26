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

#undef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define CRLF "\r\n"

static const uint64_t header_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t body_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t response_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

struct dummy_handler {
	struct cio_http_location_handler handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
};

static const size_t read_buffer_size = 200;

DEFINE_FFF_GLOBALS

static void socket_close(struct cio_server_socket *context);
FAKE_VOID_FUNC(socket_close, struct cio_server_socket *)

static void serve_error(struct cio_http_server *server, const char *reason);
FAKE_VOID_FUNC(serve_error, struct cio_http_server *, const char *)

static enum cio_error socket_set_reuse_address(struct cio_server_socket *context, bool on);
FAKE_VALUE_FUNC(enum cio_error, socket_set_reuse_address, struct cio_server_socket *, bool)

static enum cio_error socket_bind(struct cio_server_socket *context, const char *bind_address, uint16_t port);
FAKE_VALUE_FUNC(enum cio_error, socket_bind, struct cio_server_socket *, const char *, uint16_t)

FAKE_VALUE_FUNC(enum cio_error, cio_serversocket_init, struct cio_server_socket *, struct cio_eventloop *, unsigned int, cio_alloc_client, cio_free_client, cio_server_socket_close_hook)
FAKE_VALUE_FUNC(enum cio_error, cio_serversocket_accept, struct cio_server_socket *, cio_accept_handler, void *)

static enum cio_error timer_cancel(struct cio_timer *t);
FAKE_VALUE_FUNC(enum cio_error, timer_cancel, struct cio_timer *)

static void timer_close(struct cio_timer *t);
FAKE_VOID_FUNC(timer_close, struct cio_timer *)

static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, timer_expires_from_now, struct cio_timer *, uint64_t, cio_timer_handler, void *)

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)

static enum cio_error bs_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler, void *)

FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_init, struct cio_buffered_stream *, struct cio_io_stream *)

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

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *loop, cio_timer_close_hook hook)
{
	(void)loop;
	timer->cancel = timer_cancel;
	timer->close = timer_close;
	timer->close_hook = hook;
	timer->expires_from_now = timer_expires_from_now;
	return CIO_SUCCESS;
}

static enum cio_error cio_serversocket_init_ok(struct cio_server_socket *ss,
                                                struct cio_eventloop *loop,
                                                unsigned int backlog,
                                                cio_alloc_client alloc_client,
                                                cio_free_client free_client,
                                                cio_server_socket_close_hook close_hook)
{
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->backlog = (int)backlog;
	ss->impl.loop = loop;
	ss->close_hook = close_hook;
	ss->close = socket_close;
	ss->set_reuse_address = socket_set_reuse_address;
	ss->bind = socket_bind;
	return CIO_SUCCESS;
}

static enum cio_http_cb_return header_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, header_complete, struct cio_http_client *)

static enum cio_http_cb_return message_complete(struct cio_http_client *c);
FAKE_VALUE_FUNC(enum cio_http_cb_return, message_complete, struct cio_http_client *)

static struct cio_io_stream *get_io_stream(struct cio_socket *context);
FAKE_VALUE_FUNC(struct cio_io_stream *, get_io_stream, struct cio_socket *)

static enum cio_error bs_read_until(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_read_until, struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *)
static enum cio_error bs_read_at_least(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_read_at_least, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *)

static enum cio_error bs_close(struct cio_buffered_stream *bs);
FAKE_VALUE_FUNC(enum cio_error, bs_close, struct cio_buffered_stream *)

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
	client->socket.get_io_stream = get_io_stream;
	client->socket.close_hook = free_dummy_client;
	return &client->socket;
}

static void free_dummy_handler(struct cio_http_location_handler *handler)
{
	struct dummy_handler *dh = cio_container_of(handler, struct dummy_handler, handler);
	free(dh);
}

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

static struct cio_http_location_handler *alloc_dummy_handler(const void *config)
{
	(void)config;
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

static enum cio_error accept_save_handler(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	ss->handler = handler;
	ss->handler_context = handler_context;
	return CIO_SUCCESS;
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
		handler(bs, handler_context, CIO_EOF, buffer, 0);
	} else {
		const char *line = request_lines[current_line];
		size_t length = strlen(line);
		memcpy(buffer->add_ptr, line, length);
		buffer->add_ptr += length;
		current_line++;
		handler(bs, handler_context, CIO_SUCCESS, buffer, length);
	}

	return CIO_SUCCESS;
}

static enum cio_error bs_read_until_ok(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)delim;

	return bs_read_internal(bs, buffer, handler, handler_context);
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

static enum cio_error bs_read_at_least_call_fails(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	(void)bs;
	(void)buffer;
	(void)num;
	(void)handler;
	(void)handler_context;
	return CIO_BAD_FILE_DESCRIPTOR;
}

static void close_client(struct cio_http_client *client)
{
	free_dummy_client(&client->socket);
}

static enum cio_error bs_close_ok(struct cio_buffered_stream *bs)
{
	struct cio_http_client *client = cio_container_of(bs, struct cio_http_client, bs);
	close_client(client);
	return CIO_SUCCESS;
}

static enum cio_error bs_close_fails(struct cio_buffered_stream *bs)
{
	(void)bs;
	return CIO_BAD_FILE_DESCRIPTOR;
}

static uint8_t write_buffer[1000];
static size_t write_pos;

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

static enum cio_error bs_write_error(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)buf;
	handler(bs, handler_context, CIO_ADDRESS_IN_USE);
	return CIO_SUCCESS;
}

static enum cio_error cio_buffered_stream_init_ok(struct cio_buffered_stream *bs,
                                                  struct cio_io_stream *stream)
{
	(void)stream;
	bs->read_until = bs_read_until;
	bs->read_at_least = bs_read_at_least;
	bs->write = bs_write;
	bs->close = bs_close;

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

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(bs_close);
	RESET_FAKE(bs_read_at_least);
	RESET_FAKE(bs_read_until);
	RESET_FAKE(cio_buffered_stream_init);
	RESET_FAKE(cio_serversocket_accept);
	RESET_FAKE(cio_serversocket_init);
	RESET_FAKE(get_io_stream);
	RESET_FAKE(header_complete);
	RESET_FAKE(message_complete);
	RESET_FAKE(serve_error);
	RESET_FAKE(socket_bind);
	RESET_FAKE(socket_close);
	RESET_FAKE(socket_set_reuse_address);
	RESET_FAKE(timer_cancel);
	RESET_FAKE(timer_expires_from_now);

	http_parser_settings_init(&parser_settings);
	http_parser_init(&parser, HTTP_RESPONSE);

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
	timer_cancel_fake.custom_fake = cancel_timer;
	timer_expires_from_now_fake.custom_fake = expires;

	bs_read_until_fake.custom_fake = bs_read_until_ok;
	bs_close_fake.custom_fake = bs_close_ok;
	current_line = 0;
	memset(write_buffer, 0xaf, sizeof(write_buffer));
	write_pos = 0;

	bs_write_fake.custom_fake = bs_write_all;
	cio_buffered_stream_init_fake.custom_fake = cio_buffered_stream_init_ok;
	socket_close_fake.custom_fake = close_server_socket;

	get_io_stream_fake.return_val = (struct cio_io_stream *)1;
}

void tearDown(void)
{
}

static void test_serve_correctly(void)
{
	cio_serversocket_init_fake.custom_fake = cio_serversocket_init_ok;
	cio_serversocket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_write_response;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char start_line[] = "GET /foo HTTP/1.1" CRLF;

	const char *request[] = {
	    start_line,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(0, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(200);
	// Because the response is written in on_headers_complete, on_message_complete will not be called
	TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
}

static void test_read_until_errors(void)
{
	struct test {
		unsigned int which_read_until_fails;
		int expected_response;
	};

#define NUM_TESTS 4u
	static const struct test tests[] = {
	    {.which_read_until_fails = 0, .expected_response = 0},
	    {.which_read_until_fails = 1, .expected_response = 500},
	    {.which_read_until_fails = 2, .expected_response = 500},
	    {.which_read_until_fails = 3, .expected_response = 500},
	};

	enum cio_error (*bs_read_until_fakes[NUM_TESTS])(struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *);
	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct test test = tests[i];

		unsigned int array_size = ARRAY_SIZE(bs_read_until_fakes);
		for (unsigned int j = 0; j < array_size; j++) {
			bs_read_until_fakes[j] = bs_read_until_ok;
		}

		bs_read_until_fake.custom_fake = NULL;
		bs_read_until_fakes[test.which_read_until_fails] = bs_read_until_call_fails;
		SET_CUSTOM_FAKE_SEQ(bs_read_until, bs_read_until_fakes, (int)array_size);

		cio_serversocket_init_fake.custom_fake = cio_serversocket_init_ok;
		cio_serversocket_accept_fake.custom_fake = accept_save_handler;

		header_complete_fake.custom_fake = header_complete_write_response;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		const char start_line[] = "GET /foo HTTP/1.1" CRLF;

		const char *request[] = {
		    start_line,
		    "foo: bar" CRLF,
		    CRLF,
		    start_line,
		    "foo: bar" CRLF,
		    CRLF};

		init_request(request, ARRAY_SIZE(request));
		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
		check_http_response(test.expected_response);

		setUp();
	}
}

static void test_close_error(void)
{
	cio_serversocket_init_fake.custom_fake = cio_serversocket_init_ok;
	cio_serversocket_accept_fake.custom_fake = accept_save_handler;
	bs_close_fake.custom_fake = bs_close_fails;

	header_complete_fake.custom_fake = header_complete_write_response;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char start_line[] = "GET /foo HTTP/1.1" CRLF;

	const char *request[] = {
	    start_line,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was called!");
	check_http_response(200);
	// Because the response is written in on_headers_complete, on_message_complete will not be called
	TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
}

static void test_read_at_least_error(void)
{
	bs_read_at_least_fake.custom_fake = bs_read_at_least_call_fails;
	cio_serversocket_init_fake.custom_fake = cio_serversocket_init_ok;
	cio_serversocket_accept_fake.custom_fake = accept_save_handler;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char start_line[] = "GET /foo HTTP/1.1" CRLF;

	const char *request[] = {
	    start_line,
	    "Content-Length: 5" CRLF,
	    CRLF,
	    "Hello",
	    start_line,
	    "Content-Length: 5" CRLF,
	    CRLF,
	    "Hello"};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was not called!");
	check_http_response(500);
}

static void test_write_error(void)
{
	cio_serversocket_init_fake.custom_fake = cio_serversocket_init_ok;
	cio_serversocket_accept_fake.custom_fake = accept_save_handler;
	header_complete_fake.custom_fake = header_complete_write_response;
	bs_write_fake.custom_fake = bs_write_error;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char start_line[] = "GET /foo HTTP/1.1" CRLF;

	const char *request[] = {
	    start_line,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, serve_error_fake.call_count, "Serve error callback was called!");
	// Because the response is written in on_headers_complete, on_message_complete will not be called
	TEST_ASSERT_EQUAL_MESSAGE(0, message_complete_fake.call_count, "message_complete was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_read_until_errors);
	RUN_TEST(test_serve_correctly);
	RUN_TEST(test_close_error);
	RUN_TEST(test_read_at_least_error);
	RUN_TEST(test_write_error);

	return UNITY_END();
}
