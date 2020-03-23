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

#include <stdio.h>
#include <stdlib.h>

#include "cio_buffered_stream.h"
#include "cio_http_server.h"
#include "cio_util.h"
#include "cio_websocket_location_handler.h"
#include "cio_websocket_masking.h"

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

FAKE_VOID_FUNC(cio_http_location_handler_init, struct cio_http_location_handler *)
FAKE_VALUE_FUNC(enum cio_error, cio_websocket_init, struct cio_websocket *, bool, cio_websocket_on_connect, cio_websocket_close_hook)
FAKE_VOID_FUNC(fake_handler_free, struct cio_websocket_location_handler *)
FAKE_VOID_FUNC(fake_add_response_header, struct cio_http_client *, struct cio_write_buffer *)
FAKE_VALUE_FUNC(enum cio_error, fake_write_response, struct cio_http_client *, enum cio_http_status_code , struct cio_write_buffer *, cio_response_written_cb)
FAKE_VOID_FUNC(on_connect, struct cio_websocket *)

static enum cio_error write_response_call_callback (struct cio_http_client *client, enum cio_http_status_code status, struct cio_write_buffer *buf, cio_response_written_cb response_callback)
{
	(void) status;
	(void)buf;

	response_callback(client, CIO_SUCCESS);
	return CIO_SUCCESS;
}

static enum cio_error websocket_init_save_params(struct cio_websocket *ws, bool is_server, cio_websocket_on_connect on_connect_cb, cio_websocket_close_hook close_hook)
{
	ws->on_connect = on_connect_cb;
	ws->ws_private.close_hook = close_hook;
	ws->ws_private.ws_flags.is_server = is_server;
	return CIO_SUCCESS;
}

#if 0

#define REQUEST_TARGET "/ws/"
#define CRLF "\r\n"

struct ws_test_handler {
	struct cio_websocket_location_handler ws_handler;
};

static void serve_error(struct cio_http_server *server, const char *reason);
FAKE_VOID_FUNC(serve_error, struct cio_http_server *, const char *)

FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_accept, struct cio_server_socket *, cio_accept_handler, void *)
FAKE_VOID_FUNC(cio_server_socket_close, struct cio_server_socket *)
FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_init, struct cio_server_socket *, struct cio_eventloop *, unsigned int, enum cio_address_family, cio_alloc_client, cio_free_client, uint64_t, cio_server_socket_close_hook)
FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_bind, struct cio_server_socket *, const struct cio_socket_address *)
FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_set_reuse_address, struct cio_server_socket *, bool)

FAKE_VALUE_FUNC(struct cio_io_stream *, cio_socket_get_io_stream, struct cio_socket *)

FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_read_until, struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_read_at_least, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_close, struct cio_buffered_stream *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_write, struct cio_buffered_stream *, struct cio_write_buffer *, cio_buffered_stream_write_handler, void *)
FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_init, struct cio_buffered_stream *, struct cio_io_stream *)

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)
FAKE_VALUE_FUNC(enum cio_error, cio_timer_cancel, struct cio_timer *)
FAKE_VOID_FUNC(cio_timer_close, struct cio_timer *)
FAKE_VALUE_FUNC(enum cio_error, cio_timer_expires_from_now, struct cio_timer *, uint64_t, cio_timer_handler, void *)

FAKE_VALUE_FUNC(enum cio_error, cio_init_inet_address, struct cio_inet_address *, const uint8_t *, size_t)

FAKE_VALUE_FUNC(enum cio_error, cio_init_inet_socket_address, struct cio_socket_address *, const struct cio_inet_address *, uint16_t)

static void on_control(const struct cio_websocket *ws, enum cio_websocket_frame_type type, const uint8_t *data, uint_fast8_t length);
FAKE_VOID_FUNC(on_control, const struct cio_websocket *, enum cio_websocket_frame_type, const uint8_t *, uint_fast8_t)

static struct cio_eventloop loop;
static const uint64_t header_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t body_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t response_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const size_t read_buffer_size = 2000;

static enum cio_error timer_expires_from_now_ok(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context)
{
	(void)timeout_ns;

	t->handler = handler;
	t->handler_context = handler_context;

	return CIO_SUCCESS;
}

static enum cio_error cancel_timer(struct cio_timer *t)
{
	t->handler(t, t->handler_context, CIO_OPERATION_ABORTED);
	t->handler = NULL;
	return CIO_SUCCESS;
}

static void free_dummy_client(struct cio_socket *socket)
{
	struct cio_http_client *client = cio_container_of(socket, struct cio_http_client, socket);
	free(client);
}

static void bs_init(struct cio_buffered_stream *bs)
{
	bs->callback_is_running = 0;
	bs->shall_close = false;
	cio_write_buffer_head_init(&bs->wbh);
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
	return &client->socket;
}

static enum cio_error cio_server_socket_init_ok(struct cio_server_socket *ss,
                                                struct cio_eventloop *l,
                                                unsigned int backlog,
                                                enum cio_address_family family,
                                                cio_alloc_client alloc_client,
                                                cio_free_client free_client,
                                                uint64_t close_timeout_ns,
                                                cio_server_socket_close_hook close_hook)
{
	(void)close_timeout_ns;
	(void)family;
	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->backlog = (int)backlog;
	ss->impl.loop = l;
	ss->close_hook = close_hook;
	return CIO_SUCCESS;
}

static enum cio_error cio_timer_init_ok(struct cio_timer *timer, struct cio_eventloop *l, cio_timer_close_hook hook)
{
	(void)l;
	timer->close_hook = hook;
	return CIO_SUCCESS;
}

static void free_websocket_handler(struct cio_websocket_location_handler *wslh)
{
	struct ws_test_handler *h = cio_container_of(wslh, struct ws_test_handler, ws_handler);
	free(h);
}

static void on_connect(struct cio_websocket *ws)
{
	ws->ws_private.close_hook(ws);
}

static struct cio_http_location_handler *alloc_websocket_handler(const void *config)
{
	(void)config;
	struct ws_test_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	} else {
		static const char *subprotocols[2] = {"echo", "jet"};
		cio_websocket_location_handler_init(&handler->ws_handler, subprotocols, ARRAY_SIZE(subprotocols), on_connect, free_websocket_handler);
		cio_websocket_set_on_control_cb(&handler->ws_handler.websocket, on_control);
		return &handler->ws_handler.http_location;
	}
}

static struct cio_http_location_handler *alloc_websocket_handler_no_subprotocol(const void *config)
{
	(void)config;
	struct ws_test_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_websocket_location_handler_init(&handler->ws_handler, NULL, 0, on_connect, free_websocket_handler);
		return &handler->ws_handler.http_location;
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

static uint8_t http_response_write_buffer[1000];
static size_t http_response_write_pos;

static uint8_t ws_frame_write_buffer[1000];
static size_t ws_frame_write_pos;

static enum cio_error bs_write_http_response(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	size_t buffer_len = cio_write_buffer_get_num_buffer_elements(buf);
	const struct cio_write_buffer *data_buf = buf;

	for (unsigned int i = 0; i < buffer_len; i++) {
		data_buf = data_buf->next;
		memcpy(&http_response_write_buffer[http_response_write_pos], data_buf->data.element.const_data, data_buf->data.element.length);
		http_response_write_pos += data_buf->data.element.length;
	}

	handler(bs, handler_context, CIO_SUCCESS);
	return CIO_SUCCESS;
}

static enum cio_error bs_fake_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	return bs_write_http_response(bs, buf, handler, handler_context);
}

static enum cio_error bs_fake_write_error(struct cio_buffered_stream *bs, struct cio_write_buffer *buffer, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)buffer;

	handler(bs, handler_context, CIO_MESSAGE_TOO_LONG);
	return CIO_SUCCESS;
}

static void init_request(const char **request, size_t lines)
{
	request_lines = request;
	num_of_request_lines = lines;
}

static http_parser parser;
static http_parser_settings parser_settings;

static void check_http_response(int status_code)
{
	size_t nparsed = http_parser_execute(&parser, &parser_settings, (const char *)http_response_write_buffer, http_response_write_pos);
	(void)nparsed;
	TEST_ASSERT_EQUAL_MESSAGE(http_response_write_pos, nparsed, "Not a valid http response!");
	TEST_ASSERT_EQUAL_MESSAGE(status_code, parser.status_code, "http response status code not correct!");
}
#endif

void setUp(void)
{
	FFF_RESET_HISTORY()

	RESET_FAKE(cio_http_location_handler_init)
	RESET_FAKE(cio_websocket_init)
	RESET_FAKE(fake_handler_free)
	RESET_FAKE(fake_add_response_header);
	RESET_FAKE(fake_write_response);
	RESET_FAKE(on_connect);

	cio_websocket_init_fake.custom_fake = websocket_init_save_params;
	fake_write_response_fake.custom_fake = write_response_call_callback;

#if 0
	RESET_FAKE(cio_buffered_stream_init)
	RESET_FAKE(cio_buffered_stream_close)
	RESET_FAKE(cio_buffered_stream_read_at_least)
	RESET_FAKE(cio_buffered_stream_read_until)
	RESET_FAKE(cio_buffered_stream_write)

	RESET_FAKE(cio_server_socket_accept)
	RESET_FAKE(cio_server_socket_bind)
	RESET_FAKE(cio_server_socket_init)
	RESET_FAKE(cio_server_socket_set_reuse_address)

	RESET_FAKE(cio_timer_init)
	RESET_FAKE(cio_timer_cancel)
	RESET_FAKE(cio_timer_close)
	RESET_FAKE(cio_timer_expires_from_now)

	RESET_FAKE(on_control)
	RESET_FAKE(serve_error)

	http_parser_settings_init(&parser_settings);
	http_parser_init(&parser, HTTP_RESPONSE);

	current_line = 0;
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	cio_server_socket_accept_fake.custom_fake = accept_save_handler;

	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
	cio_buffered_stream_read_until_fake.custom_fake = bs_read_until_ok;
	cio_buffered_stream_close_fake.custom_fake = bs_close_ok;

	memset(http_response_write_buffer, 0xaf, sizeof(http_response_write_buffer));
	http_response_write_pos = 0;

	memset(ws_frame_write_buffer, 0xaf, sizeof(ws_frame_write_buffer));
	ws_frame_write_pos = 0;

	cio_timer_expires_from_now_fake.custom_fake = timer_expires_from_now_ok;
	cio_timer_cancel_fake.custom_fake = cancel_timer;

	cio_socket_get_io_stream_fake.return_val = (struct cio_io_stream *)1;
#endif
}

void tearDown(void)
{
}

#if 0
struct ws_version_test {
	const char *version_line;
	int status_code;
};

static void test_ws_version(void)
{
	struct ws_version_test test_cases[] = {
	    {.version_line = "Sec-WebSocket-Version: 13" CRLF, .status_code = 101},
	    {.version_line = "Sec-WebSocket-Versiin: 13" CRLF, .status_code = 400},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct ws_version_test test_case = test_cases[i];
		cio_buffered_stream_write_fake.custom_fake = bs_fake_write;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		uint8_t ip[4] = {0, 0, 0, 0};
		struct cio_inet_address address;
		cio_init_inet_address(&address, ip, sizeof(ip));
		cio_init_inet_socket_address(&config.endpoint, &address, 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		const char *request[] = {
		    "GET " REQUEST_TARGET " HTTP/1.1" CRLF,
		    "Upgrade: websocket" CRLF,
		    "Connection: Upgrade" CRLF,
		    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" CRLF,
		    "Sec-WebSocket-Protocol: jet" CRLF,
		    test_case.version_line,
		    CRLF};

		init_request(request, ARRAY_SIZE(request));
		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		check_http_response(test_case.status_code);

		setUp();
	}
}

static void test_ws_location_wrong_http_version(void)
{
	struct http_test {
		const char *start_line;
		int status_code;
	};

	struct http_test test_cases[] = {
	    {.start_line = "GET " REQUEST_TARGET " HTTP/1.1" CRLF, .status_code = 101},
	    {.start_line = "GET " REQUEST_TARGET " HTTP/1.0" CRLF, .status_code = 400},
	    {.start_line = "GET " REQUEST_TARGET " HTTP/2.0" CRLF, .status_code = 101},
	    {.start_line = "GET " REQUEST_TARGET " HTTP/1.2" CRLF, .status_code = 101},
	    {.start_line = "GET " REQUEST_TARGET CRLF, .status_code = 400},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct http_test test_case = test_cases[i];

		cio_buffered_stream_write_fake.custom_fake = bs_fake_write;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		uint8_t ip[4] = {0, 0, 0, 0};
		struct cio_inet_address address;
		cio_init_inet_address(&address, ip, sizeof(ip));
		cio_init_inet_socket_address(&config.endpoint, &address, 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		const char *request[] = {
		    test_case.start_line,
		    "Upgrade: websocket" CRLF,
		    "Connection: Upgrade" CRLF,
		    "Sec-WebSocket-Version: 13" CRLF,
		    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" CRLF,
		    "Sec-WebSocket-Protocol: jet" CRLF,
		    CRLF};

		init_request(request, ARRAY_SIZE(request));
		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		check_http_response(test_case.status_code);

		setUp();
	}
}

static void test_ws_location_wrong_http_method(void)
{
	cio_buffered_stream_write_fake.custom_fake = bs_fake_write;
	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = 10,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	uint8_t ip[4] = {0, 0, 0, 0};
	struct cio_inet_address address;
	cio_init_inet_address(&address, ip, sizeof(ip));
	cio_init_inet_socket_address(&config.endpoint, &address, 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    "PUT " REQUEST_TARGET " HTTP/1.1" CRLF,
	    "Upgrade: websocket" CRLF,
	    "Connection: Upgrade" CRLF,
	    "Sec-WebSocket-Version: 13" CRLF,
	    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" CRLF,
	    "Sec-WebSocket-Protocol: jet" CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	check_http_response(400);
}

static void test_ws_location_wrong_ws_version(void)
{
	for (signed int i = -10; i < 13; i++) {
		cio_buffered_stream_write_fake.custom_fake = bs_fake_write;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		uint8_t ip[4] = {0, 0, 0, 0};
		struct cio_inet_address address;
		cio_init_inet_address(&address, ip, sizeof(ip));
		cio_init_inet_socket_address(&config.endpoint, &address, 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		char version_buffer[100];
		snprintf(version_buffer, sizeof(version_buffer) - 1, "Sec-WebSocket-Version: %d" CRLF, i);
		const char *request[] = {
		    "GET " REQUEST_TARGET " HTTP/1.1" CRLF,
		    "Upgrade: websocket" CRLF,
		    "Connection: Upgrade" CRLF,
		    version_buffer,
		    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" CRLF,
		    "Sec-WebSocket-Protocol: jet" CRLF,
		    CRLF};

		init_request(request, ARRAY_SIZE(request));
		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		check_http_response(400);

		setUp();
	}
}

struct key_test {
	const char *key_line;
	int status_code;
};

static void test_ws_key(void)
{
	struct key_test test_cases[] = {
	    {.key_line = "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" CRLF, .status_code = 101},
	    {.key_line = "Sec-WebSocket-Kez: dGhlIHNhbXBsZSBub25jZQ==" CRLF, .status_code = 400},
	    {.key_line = "foo: bar" CRLF, .status_code = 400},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct key_test test_case = test_cases[i];
		cio_buffered_stream_write_fake.custom_fake = bs_fake_write;

		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		uint8_t ip[4] = {0, 0, 0, 0};
		struct cio_inet_address address;
		cio_init_inet_address(&address, ip, sizeof(ip));
		cio_init_inet_socket_address(&config.endpoint, &address, 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		const char *request[] = {
		    "GET " REQUEST_TARGET " HTTP/1.1" CRLF,
		    "Upgrade: websocket" CRLF,
		    "Connection: Upgrade" CRLF,
		    "Sec-WebSocket-Version: 13" CRLF,
		    test_case.key_line,
		    "Sec-WebSocket-Protocol: jet" CRLF,
		    CRLF};

		init_request(request, ARRAY_SIZE(request));
		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		check_http_response(test_case.status_code);

		setUp();
	}
}

static void test_ws_location_wrong_key_length(void)
{
	cio_buffered_stream_write_fake.custom_fake = bs_fake_write;

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = 10,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	uint8_t ip[4] = {0, 0, 0, 0};
	struct cio_inet_address address;
	cio_init_inet_address(&address, ip, sizeof(ip));
	cio_init_inet_socket_address(&config.endpoint, &address, 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    "GET " REQUEST_TARGET " HTTP/1.1" CRLF,
	    "Upgrade: websocket" CRLF,
	    "Connection: Upgrade" CRLF,
	    "Sec-WebSocket-Key: dGhlIHNhBsZSBub25jZQ==" CRLF,
	    "Sec-WebSocket-Version: 13" CRLF,
	    "Sec-WebSocket-Protocol: jet" CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	check_http_response(400);
}

struct protocol_test {
	const char *protocol_line;
	cio_http_alloc_handler handler;
	int status_code;
};

static void test_ws_location_subprotocols(void)
{
	struct protocol_test test_cases[] = {
	    {.protocol_line = "Sec-WebSocket-Protocal: jet" CRLF, .handler = alloc_websocket_handler, .status_code = 101},
	    {.protocol_line = "Sec-WebSocket-Protocol: jet" CRLF, .handler = alloc_websocket_handler, .status_code = 101},
	    {.protocol_line = "Sec-WebSocket-Protocol: jet,jetty" CRLF, .handler = alloc_websocket_handler, .status_code = 101},
	    {.protocol_line = "Sec-WebSocket-Protocol: foo, bar" CRLF, .handler = alloc_websocket_handler, .status_code = 400},
	    {.protocol_line = "Sec-WebSocket-Protocol: je" CRLF, .handler = alloc_websocket_handler, .status_code = 400},
	    {.protocol_line = "Sec-WebSocket-Protocol: bar" CRLF, .handler = alloc_websocket_handler, .status_code = 400},
	    {.protocol_line = "foo: bar" CRLF, .handler = alloc_websocket_handler, .status_code = 101},
	    {.protocol_line = "foo: bar" CRLF, .handler = alloc_websocket_handler_no_subprotocol, .status_code = 101},
	    {.protocol_line = "Sec-WebSocket-Protocol: jet" CRLF, .handler = alloc_websocket_handler_no_subprotocol, .status_code = 400},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct protocol_test test_case = test_cases[i];

		cio_buffered_stream_write_fake.custom_fake = bs_fake_write;
		struct cio_http_server_configuration config = {
		    .on_error = serve_error,
		    .read_header_timeout_ns = header_read_timeout,
		    .read_body_timeout_ns = body_read_timeout,
		    .response_timeout_ns = response_timeout,
		    .close_timeout_ns = 10,
		    .alloc_client = alloc_dummy_client,
		    .free_client = free_dummy_client};

		uint8_t ip[4] = {0, 0, 0, 0};
		struct cio_inet_address address;
		cio_init_inet_address(&address, ip, sizeof(ip));
		cio_init_inet_socket_address(&config.endpoint, &address, 8080);

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, &loop, &config);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, test_case.handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = cio_http_server_register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = cio_http_server_serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		const char *request[] = {
		    "GET " REQUEST_TARGET " HTTP/1.1" CRLF,
		    "Upgrade: websocket" CRLF,
		    "Connection: Upgrade" CRLF,
		    "Sec-WebSocket-Version: 13" CRLF,
		    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" CRLF,
		    test_case.protocol_line,
		    CRLF};

		init_request(request, ARRAY_SIZE(request));
		server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
		check_http_response(test_case.status_code);

		setUp();
	}
}

static void test_ws_location_no_upgrade(void)
{
	cio_buffered_stream_write_fake.custom_fake = bs_fake_write;

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = 10,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	uint8_t ip[4] = {0, 0, 0, 0};
	struct cio_inet_address address;
	cio_init_inet_address(&address, ip, sizeof(ip));
	cio_init_inet_socket_address(&config.endpoint, &address, 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    "GET " REQUEST_TARGET " HTTP/1.1" CRLF,
	    "Upgrade: websocket" CRLF,
	    "Sec-WebSocket-Version: 13" CRLF,
	    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" CRLF,
	    "Sec-WebSocket-Protocol: jet" CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	check_http_response(400);
}

static void test_ws_location_write_error(void)
{
	cio_buffered_stream_write_fake.custom_fake = bs_fake_write_error;

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = header_read_timeout,
	    .read_body_timeout_ns = body_read_timeout,
	    .response_timeout_ns = response_timeout,
	    .close_timeout_ns = 10,
	    .alloc_client = alloc_dummy_client,
	    .free_client = free_dummy_client};

	uint8_t ip[4] = {0, 0, 0, 0};
	struct cio_inet_address address;
	cio_init_inet_address(&address, ip, sizeof(ip));
	cio_init_inet_socket_address(&config.endpoint, &address, 8080);

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, &loop, &config);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = cio_http_server_register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = cio_http_server_serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();

	const char *request[] = {
	    "GET " REQUEST_TARGET " HTTP/1.1" CRLF,
	    "Upgrade: websocket" CRLF,
	    "Connection: Upgrade" CRLF,
	    "Sec-WebSocket-Version: 13" CRLF,
	    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" CRLF,
	    "Sec-WebSocket-Protocol: jet" CRLF,
	    CRLF};

	init_request(request, ARRAY_SIZE(request));
	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, CIO_SUCCESS, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, cio_buffered_stream_close_fake.call_count, "Stream was not closed in case of error!");
}
#endif

static void test_ws_location_init_ok(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, NULL, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");
}

static void test_ws_location_ws_init_fails(void)
{
	struct cio_websocket_location_handler handler;
	cio_websocket_init_fake.custom_fake = NULL;
	cio_websocket_init_fake.return_val = CIO_INVALID_ARGUMENT;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, NULL, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "web socket handler initialization didn't fail!");
}

static void test_ws_location_init_fails(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(NULL, NULL, 0, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "web socket handler initialization did not failed if no handler is provided!");

	err = cio_websocket_location_handler_init(&handler, NULL, 0, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "web socket handler initialization did not failed if no location free function is provided!");
}

static void test_free_resources(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, NULL, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

	handler.http_location.free(&handler.http_location);
	TEST_ASSERT_EQUAL_MESSAGE(1, fake_handler_free_fake.call_count, "free_resources was not called when http_location is freed!");
}

static void test_ws_location_http_versions(void)
{
	struct upgrade_test {
		uint16_t major;
		uint16_t minor;
		enum cio_http_cb_return expected_ret_val;
	};

	struct upgrade_test tests[] = {
		{.major = 1, .minor = 1, .expected_ret_val = CIO_HTTP_CB_SKIP_BODY},
		{.major = 2, .minor = 0, .expected_ret_val = CIO_HTTP_CB_SKIP_BODY},
		{.major = 1, .minor = 0, .expected_ret_val = CIO_HTTP_CB_ERROR},
		{.major = 0, .minor = 9, .expected_ret_val = CIO_HTTP_CB_ERROR},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct upgrade_test test = tests[i];

		struct cio_websocket_location_handler handler;
		enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

		struct cio_http_client client;

		handler.websocket.ws_private.http_client =  &client;
		handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
		handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
		handler.websocket.ws_private.http_client->write_response = fake_write_response;

		handler.websocket.ws_private.http_client->parser.upgrade = 1;
		handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
		handler.websocket.ws_private.http_client->http_major = test.major;
		handler.websocket.ws_private.http_client->http_minor = test.minor;

		static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
		static const char sec_ws_version_value[] = "13";
		enum cio_http_cb_return cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
		cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

		static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
		static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
		cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
		cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

		cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
		TEST_ASSERT_EQUAL_MESSAGE(test.expected_ret_val, cb_ret, "on_header_complete returned wrong value");
		if (test.expected_ret_val == CIO_HTTP_CB_SKIP_BODY) {
			TEST_ASSERT_EQUAL_MESSAGE(1, fake_write_response_fake.call_count, "write_response was not called");
			TEST_ASSERT_EQUAL_MESSAGE(1, on_connect_fake.call_count, "websocket on_connect was not called");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_STATUS_SWITCHING_PROTOCOLS, fake_write_response_fake.arg1_val, "write_response was not called with CIO_HTTP_STATUS_SWITCHING_PROTOCOLS");
		}

		setUp();
	}
}

static void test_ws_location_wrong_http_method(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

	struct cio_http_client client;

	handler.websocket.ws_private.http_client =  &client;
	handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
	handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
	handler.websocket.ws_private.http_client->write_response = fake_write_response;

	handler.websocket.ws_private.http_client->parser.upgrade = 1;
	handler.websocket.ws_private.http_client->http_method = CIO_HTTP_POST;
	handler.websocket.ws_private.http_client->http_major = 1;
	handler.websocket.ws_private.http_client->http_minor = 1;

	static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
	static const char sec_ws_version_value[] = "13";
	enum cio_http_cb_return cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
	cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

	static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
	static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
	cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
	cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

	cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_ERROR, cb_ret, "on_header_complete returned wrong value");
}

static void test_ws_location_no_http_upgrade(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

	struct cio_http_client client;

	handler.websocket.ws_private.http_client =  &client;
	handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
	handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
	handler.websocket.ws_private.http_client->write_response = fake_write_response;

	handler.websocket.ws_private.http_client->parser.upgrade = 0;
	handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
	handler.websocket.ws_private.http_client->http_major = 1;
	handler.websocket.ws_private.http_client->http_minor = 1;

	static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
	static const char sec_ws_version_value[] = "13";
	enum cio_http_cb_return cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
	cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

	static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
	static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
	cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
	cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

	cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_ERROR, cb_ret, "on_header_complete returned wrong value");
}

static void test_ws_location_wrong_http_headers(void)
{
	struct upgrade_test {
		const char *sec_key_field;
		const char *sec_key_value;
		const char *version_field;
		const char *version_value;
		enum cio_http_cb_return on_header_value_retval_version;
		enum cio_http_cb_return on_header_value_retval_key;
		enum cio_http_cb_return on_headers_retval;
	};

	struct upgrade_test tests[] = {
		{
			.sec_key_field = "Sec-WebSocket-Key",
			.sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
			.on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
			.version_field = "Sec-WebSocket-Version",
			.version_value = "13",
			.on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
			.on_headers_retval = CIO_HTTP_CB_SKIP_BODY
		},
		{
			.sec_key_field = "Sic-WebSocket-Key",
			.sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
			.on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
			.version_field = "Sec-WebSocket-Version",
			.version_value = "13",
			.on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
			.on_headers_retval = CIO_HTTP_CB_ERROR
		},
		{
			.sec_key_field = "Sec-WebSocket-Key",
			.sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
			.on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
			.version_field = "Sec-WebSocket-Wersion",
			.version_value = "13",
			.on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
			.on_headers_retval = CIO_HTTP_CB_ERROR
		},
		{
			.sec_key_field = "Sec-WebSocket-Key",
			.sec_key_value = "lIHNhbXBsZSBub25jZQ==",
			.on_header_value_retval_key = CIO_HTTP_CB_ERROR,
			.version_field = "Sec-WebSocket-Version",
			.version_value = "13",
			.on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
			.on_headers_retval = CIO_HTTP_CB_ERROR
		},
		{
			.sec_key_field = "Sec-WebSocket-Key",
			.sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
			.on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
			.version_field = "Sec-WebSocket-Version",
			.version_value = "12",
			.on_header_value_retval_version = CIO_HTTP_CB_ERROR,
			.on_headers_retval = CIO_HTTP_CB_ERROR
		},
		{
			.sec_key_field = "Sec-WebSocket-Key",
			.sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
			.on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
			.version_field = "Sec-WebSocket-Version",
			.version_value = "2",
			.on_header_value_retval_version = CIO_HTTP_CB_ERROR,
			.on_headers_retval = CIO_HTTP_CB_ERROR
		},

	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct upgrade_test test = tests[i];

		struct cio_websocket_location_handler handler;
		enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

		struct cio_http_client client;

		handler.websocket.ws_private.http_client =  &client;
		handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
		handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
		handler.websocket.ws_private.http_client->write_response = fake_write_response;

		handler.websocket.ws_private.http_client->parser.upgrade = 1;
		handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
		handler.websocket.ws_private.http_client->http_major = 1;
		handler.websocket.ws_private.http_client->http_minor = 1;

		enum cio_http_cb_return cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, test.version_field, strlen(test.version_field));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
		cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, test.version_value, strlen(test.version_value));
		TEST_ASSERT_EQUAL_MESSAGE(test.on_header_value_retval_version, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

		cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, test.sec_key_field, strlen(test.sec_key_field));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
		cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, test.sec_key_value, strlen(test.sec_key_value));
		TEST_ASSERT_EQUAL_MESSAGE(test.on_header_value_retval_key, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

		cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
		TEST_ASSERT_EQUAL_MESSAGE(test.on_headers_retval, cb_ret, "on_header_complete returned wrong value");
		if (test.on_headers_retval == CIO_HTTP_CB_SKIP_BODY) {
			TEST_ASSERT_EQUAL_MESSAGE(1, fake_write_response_fake.call_count, "write_response was not called");
			TEST_ASSERT_EQUAL_MESSAGE(1, on_connect_fake.call_count, "websocket on_connect was not called");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_STATUS_SWITCHING_PROTOCOLS, fake_write_response_fake.arg1_val, "write_response was not called with CIO_HTTP_STATUS_SWITCHING_PROTOCOLS");
		}

		setUp();
	}
}

static void test_ws_location_send_response_fails(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

	struct cio_http_client client;
	fake_write_response_fake.custom_fake = NULL;
	fake_write_response_fake.return_val = CIO_INVALID_ARGUMENT;

	handler.websocket.ws_private.http_client =  &client;
	handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
	handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
	handler.websocket.ws_private.http_client->write_response = fake_write_response;

	handler.websocket.ws_private.http_client->parser.upgrade = 1;
	handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
	handler.websocket.ws_private.http_client->http_major = 1;
	handler.websocket.ws_private.http_client->http_minor = 1;

	static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
	static const char sec_ws_version_value[] = "13";
	enum cio_http_cb_return cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
	cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

	static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
	static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
	cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
	cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

	cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_ERROR, cb_ret, "on_header_complete returned wrong value");
}

static void test_ws_location_sub_protocols(void)
{
	struct upgrade_test {
		const char *protocol_field;
		const char *protocol_value;
		enum cio_http_cb_return expected_ret_val;
	};

	struct upgrade_test tests[] = {
	    {.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jet", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY},
	    {.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jet,jetty",.expected_ret_val = CIO_HTTP_CB_SKIP_BODY},
	    {.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "foo, bar",.expected_ret_val = CIO_HTTP_CB_ERROR},
	    {.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "je",.expected_ret_val = CIO_HTTP_CB_ERROR},
	    {.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "bar",.expected_ret_val = CIO_HTTP_CB_ERROR},
	    {.protocol_field = "foo", .protocol_value = "bar",.expected_ret_val = CIO_HTTP_CB_SKIP_BODY},
	    //{.protocol_field = "foo", .protocol_value = "bar", .handler = alloc_websocket_handler_no_subprotocol, .expected_ret_val = CIO_HTTP_CB_SKIP_BODY},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jet", .handler = alloc_websocket_handler_no_subprotocol, .expected_ret_val = CIO_HTTP_CB_ERROR},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct upgrade_test test = tests[i];

		struct cio_websocket_location_handler handler;

		static const char *subprotocols[2] = {"echo", "jet"};
		enum cio_error err =cio_websocket_location_handler_init(&handler, subprotocols, ARRAY_SIZE(subprotocols), on_connect, fake_handler_free);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

		struct cio_http_client client;

		handler.websocket.ws_private.http_client =  &client;
		handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
		handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
		handler.websocket.ws_private.http_client->write_response = fake_write_response;

		handler.websocket.ws_private.http_client->parser.upgrade = 1;
		handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
		handler.websocket.ws_private.http_client->http_major = 1;
		handler.websocket.ws_private.http_client->http_minor = 1;

		static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
		static const char sec_ws_version_value[] = "13";
		enum cio_http_cb_return cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
		cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

		static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
		static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
		cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
		cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

		cb_ret = handler.http_location.on_header_field(handler.websocket.ws_private.http_client, test.protocol_field, strlen(test.protocol_field));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_protocol_field");
		cb_ret = handler.http_location.on_header_value(handler.websocket.ws_private.http_client, test.protocol_value, strlen(test.protocol_value));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_protocol_value");

		cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
		TEST_ASSERT_EQUAL_MESSAGE(test.expected_ret_val, cb_ret, "on_header_complete returned wrong value");
		if (test.expected_ret_val == CIO_HTTP_CB_SKIP_BODY) {
			TEST_ASSERT_EQUAL_MESSAGE(1, fake_write_response_fake.call_count, "write_response was not called");
			TEST_ASSERT_EQUAL_MESSAGE(1, on_connect_fake.call_count, "websocket on_connect was not called");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_STATUS_SWITCHING_PROTOCOLS, fake_write_response_fake.arg1_val, "write_response was not called with CIO_HTTP_STATUS_SWITCHING_PROTOCOLS");
		}

		setUp();
	}
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_ws_location_init_fails);
	RUN_TEST(test_ws_location_init_ok);
	RUN_TEST(test_ws_location_ws_init_fails);
	RUN_TEST(test_free_resources);

	RUN_TEST(test_ws_location_http_versions);;
	RUN_TEST(test_ws_location_wrong_http_method);
	RUN_TEST(test_ws_location_no_http_upgrade);
	RUN_TEST(test_ws_location_wrong_http_headers);
	RUN_TEST(test_ws_location_send_response_fails);

	RUN_TEST(test_ws_location_sub_protocols);

	//RUN_TEST(test_ws_location_wrong_http_version);
	//RUN_TEST(test_ws_location_wrong_http_method);
	//RUN_TEST(test_ws_location_wrong_ws_version);
	//RUN_TEST(test_ws_location_no_upgrade);
	//RUN_TEST(test_ws_key);
	//RUN_TEST(test_ws_location_wrong_key_length);
	//RUN_TEST(test_ws_location_subprotocols);
	//RUN_TEST(test_ws_location_write_error);
	//RUN_TEST(test_ws_version);
	return UNITY_END();
}
