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

#include <stdio.h>
#include <stdlib.h>

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

#define REQUEST_TARGET "/ws/"
#define CRLF "\r\n"

struct ws_test_handler {
	struct cio_websocket_location_handler ws_handler;
};

#define WS_HEADER_FIN 0x80
#define WS_MASK_SET 0x80

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

static struct cio_io_stream *get_io_stream(struct cio_socket *context);
FAKE_VALUE_FUNC(struct cio_io_stream *, get_io_stream, struct cio_socket *)

FAKE_VALUE_FUNC(enum cio_error, cio_buffered_stream_init, struct cio_buffered_stream *, struct cio_io_stream *)

static enum cio_error bs_read_until(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, const char *delim, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_read_until, struct cio_buffered_stream *, struct cio_read_buffer *, const char *, cio_buffered_stream_read_handler, void *)

static enum cio_error bs_read_exactly(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_read_exactly, struct cio_buffered_stream *, struct cio_read_buffer *, size_t, cio_buffered_stream_read_handler, void *)

static enum cio_error bs_read(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, cio_buffered_stream_read_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_read, struct cio_buffered_stream *, struct cio_read_buffer *, cio_buffered_stream_read_handler, void *)

static enum cio_error bs_close(struct cio_buffered_stream *bs);
FAKE_VALUE_FUNC(enum cio_error, bs_close, struct cio_buffered_stream *)

static enum cio_error bs_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buffer, cio_buffered_stream_write_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, bs_write, struct cio_buffered_stream *, struct cio_write_buffer *,cio_buffered_stream_write_handler, void *)

FAKE_VALUE_FUNC(enum cio_error, cio_timer_init, struct cio_timer *, struct cio_eventloop *, cio_timer_close_hook)

static enum cio_error timer_cancel(struct cio_timer *t);
FAKE_VALUE_FUNC(enum cio_error, timer_cancel, struct cio_timer *)

static void timer_close(struct cio_timer *t);
FAKE_VOID_FUNC(timer_close, struct cio_timer *)

static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, timer_handler handler, void *handler_context);
FAKE_VALUE_FUNC(enum cio_error, timer_expires_from_now, struct cio_timer *, uint64_t, timer_handler, void *)

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      cio_alloc_client alloc_client,
                                      cio_free_client free_client,
                                      cio_server_socket_close_hook close_hook);

FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_init, struct cio_server_socket *, struct cio_eventloop *, unsigned int, cio_alloc_client, cio_free_client, cio_server_socket_close_hook)

static void on_control(const struct cio_websocket *ws, enum cio_websocket_frame_type type, const uint8_t *data, size_t length);
FAKE_VOID_FUNC(on_control, const struct cio_websocket *, enum cio_websocket_frame_type, const uint8_t *, size_t)

static void read_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err, uint8_t *data, size_t length, bool last_frame, bool is_binary);
FAKE_VOID_FUNC(read_handler, struct cio_websocket *, void *, enum cio_error, uint8_t *, size_t, bool, bool)

static struct cio_eventloop loop;
static const uint64_t read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const size_t read_buffer_size = 2000;

static uint8_t *bs_read_exactly_buffer;
static size_t bs_read_exactly_buffer_size;
static size_t bs_read_exactly_buffer_pos;

static void free_dummy_client(struct cio_socket *socket)
{
	struct cio_http_client *client = container_of(socket, struct cio_http_client, socket);
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
	ss->loop = l;
	ss->close_hook = close_hook;
	ss->close = socket_close;
	ss->accept = socket_accept;
	ss->set_reuse_address = socket_set_reuse_address;
	ss->bind = socket_bind;
	return CIO_SUCCESS;
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

static enum cio_error cio_buffered_stream_init_ok(struct cio_buffered_stream *bs,
                                                  struct cio_io_stream *stream)
{
	(void)stream;
	bs->read_until = bs_read_until;
	bs->read_exactly = bs_read_exactly;
	bs->read = bs_read;
	bs->write = bs_write;
	bs->close = bs_close;

	return CIO_SUCCESS;
}

static void free_websocket_handler(struct cio_http_location_handler *handler)
{
	struct ws_test_handler *h = container_of(handler, struct ws_test_handler, ws_handler);
	free(h);
}

static void on_connect(struct cio_websocket *ws)
{
	ws->close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, NULL, NULL);
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Could not start reading a message!");
}

static struct cio_http_location_handler *alloc_websocket_handler(const void *config)
{
	(void)config;
	struct ws_test_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		static const char *subprotocols[2] = {"echo", "jet"};
		cio_websocket_location_handler_init(&handler->ws_handler, subprotocols, ARRAY_SIZE(subprotocols), on_connect);
		handler->ws_handler.http_location.free = free_websocket_handler;
		handler->ws_handler.websocket.on_control = on_control;
		return &handler->ws_handler.http_location;
	}
}

static struct cio_http_location_handler *alloc_websocket_handler_no_subprotocol(const void *config)
{
	(void)config;
	struct ws_test_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_websocket_location_handler_init(&handler->ws_handler, NULL, 0, on_connect);
		handler->ws_handler.http_location.free = free_websocket_handler;
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
		buffer->bytes_transferred = 0;
	} else {
		const char *line = request_lines[current_line];
		size_t length = strlen(line);
		memcpy(buffer->add_ptr, line, length);
		buffer->bytes_transferred = length;
		buffer->fetch_ptr = buffer->add_ptr + length;
		current_line++;
	}

	handler(bs, handler_context, CIO_SUCCESS, buffer);
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
	struct cio_http_client *client = container_of(bs, struct cio_http_client, bs);
	close_client(client);
	return CIO_SUCCESS;
}

static uint8_t http_response_write_buffer[1000];
static size_t http_response_write_pos;

static uint8_t ws_frame_write_buffer[1000];
static size_t ws_frame_write_pos;

static enum cio_error bs_write_http_response(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	size_t buffer_len = cio_write_buffer_get_number_of_elements(buf);
	const struct cio_write_buffer *data_buf = buf;

	for (unsigned int i = 0; i < buffer_len; i++) {
		data_buf = data_buf->next;
		memcpy(&http_response_write_buffer[http_response_write_pos], data_buf->data.element.const_data, data_buf->data.element.length);
		http_response_write_pos += data_buf->data.element.length;
	}

	handler(bs, handler_context, CIO_SUCCESS);
	return CIO_SUCCESS;
}

static enum cio_error bs_write_ws_frame(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	size_t buffer_len = cio_write_buffer_get_number_of_elements(buf);
	const struct cio_write_buffer *data_buf = buf;

	for (unsigned int i = 0; i < buffer_len; i++) {
		data_buf = data_buf->next;
		memcpy(&ws_frame_write_buffer[ws_frame_write_pos], data_buf->data.element.const_data, data_buf->data.element.length);
		ws_frame_write_pos += data_buf->data.element.length;
	}

	handler(bs, handler_context, CIO_SUCCESS);
	return CIO_SUCCESS;
}

static enum cio_error bs_read_exactly_from_buffer(struct cio_buffered_stream *bs, struct cio_read_buffer *buffer, size_t num, cio_buffered_stream_read_handler handler, void *handler_context)
{
	if (bs_read_exactly_buffer_pos >= bs_read_exactly_buffer_size) {
		buffer->bytes_transferred = 0;
	} else {
		memcpy(buffer->add_ptr, &bs_read_exactly_buffer[bs_read_exactly_buffer_pos], num);
		buffer->bytes_transferred = num;
		buffer->fetch_ptr = buffer->add_ptr + num;
		bs_read_exactly_buffer_pos += num;
	}

	handler(bs, handler_context, CIO_SUCCESS, buffer);
	return CIO_SUCCESS;
}

static enum cio_error bs_fake_write(struct cio_buffered_stream *bs, struct cio_write_buffer *buf, cio_buffered_stream_write_handler handler, void *handler_context)
{
	if (bs_write_fake.call_count == 1) {
		return bs_write_http_response(bs, buf, handler, handler_context);
	} else {
		return bs_write_ws_frame(bs, buf, handler, handler_context);
	}
}

static enum cio_error bs_fake_write_error(struct cio_buffered_stream *bs, struct cio_write_buffer *buffer, cio_buffered_stream_write_handler handler, void *handler_context)
{
	(void)buffer;

	handler(bs, handler_context, CIO_BROKEN_PIPE);
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

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(cio_buffered_stream_init);
	RESET_FAKE(cio_timer_init);
	RESET_FAKE(timer_cancel);
	RESET_FAKE(timer_close);
	RESET_FAKE(timer_expires_from_now);
	RESET_FAKE(get_io_stream);
	RESET_FAKE(serve_error);
	RESET_FAKE(socket_accept);
	RESET_FAKE(socket_bind);
	RESET_FAKE(socket_close);
	RESET_FAKE(socket_set_reuse_address);

	RESET_FAKE(bs_close);
	RESET_FAKE(bs_write);
	RESET_FAKE(bs_read_until);
	RESET_FAKE(bs_read);

	RESET_FAKE(on_control);
	RESET_FAKE(read_handler);

	http_parser_settings_init(&parser_settings);
	http_parser_init(&parser, HTTP_RESPONSE);

	current_line = 0;
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;
	cio_timer_init_fake.custom_fake = cio_timer_init_ok;
	cio_buffered_stream_init_fake.custom_fake = cio_buffered_stream_init_ok;
	bs_read_until_fake.custom_fake = bs_read_until_ok;

	memset(http_response_write_buffer, 0xaf, sizeof(http_response_write_buffer));
	http_response_write_pos = 0;

	memset(ws_frame_write_buffer, 0xaf, sizeof(ws_frame_write_buffer));
	ws_frame_write_pos = 0;

	bs_close_fake.custom_fake = bs_close_ok;
	bs_read_exactly_buffer_pos = 0;
}

static size_t assemble_frame(uint8_t header, uint8_t *mask, uint8_t *data, size_t length, uint8_t **ptr)
{
	if (length > 125) {
		*ptr = NULL;
		return 0;
	}

	size_t bytes = 1 + 1 + length;
	if (mask != NULL) {
		bytes += 4;
	}

	unsigned int offset;
	uint8_t *mem = malloc(bytes);
	mem[0] = header;
	mem[1] = length;
	if (mask != NULL) {
		mem[1] |= WS_MASK_SET;
		memcpy(&mem[2], mask, 4);
		offset = 6;
	} else {
		offset = 2;
	}

	memcpy(&mem[offset], data, length);
	if (mask != NULL) {
		cio_websocket_mask(&mem[6], length, &mem[2]);
	}

	*ptr = mem;
	return offset + length;
}

static void test_ws_location_write_error(void)
{
	uint8_t *close_frame;
	uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
	uint8_t data[2] = {0x3, 0xe8};
	bs_read_exactly_buffer_size = assemble_frame(WS_HEADER_FIN | CIO_WEBSOCKET_CLOSE_FRAME, mask, data, sizeof(data), &close_frame);

	bs_read_exactly_buffer = close_frame;

	bs_write_fake.custom_fake = bs_fake_write_error;
	bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
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
	free(close_frame);
	TEST_ASSERT_EQUAL_MESSAGE(1, bs_close_fake.call_count, "Stream was not closed in case of error!");
}

static void test_ws_location_close_in_onconnect(void)
{
	uint8_t *close_frame;
	uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
	uint8_t data[2] = {0x3, 0xe8};
	bs_read_exactly_buffer_size = assemble_frame(WS_HEADER_FIN | CIO_WEBSOCKET_CLOSE_FRAME, mask, data, sizeof(data), &close_frame);

	bs_read_exactly_buffer = close_frame;
	bs_write_fake.custom_fake = bs_fake_write;

	bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
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
	check_http_response(101);

	TEST_ASSERT_EQUAL_MESSAGE(1, on_control_fake.call_count, "control callback was not called for last close frame");
	TEST_ASSERT_NOT_NULL_MESSAGE(on_control_fake.arg0_val, "websocket parameter of control callback is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_WEBSOCKET_CLOSE_FRAME, on_control_fake.arg1_val, "websocket parameter of control callback is NULL");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(data), on_control_fake.arg3_val, "data length parameter of control callback is not correct");
	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read callback was not called for last close frame");
	free(close_frame);
}

struct http_test {
	const char *start_line;
	int status_code;
};

static void test_ws_location_wrong_http_version(void)
{
	struct http_test test_cases[] = {
	    {.start_line = "GET " REQUEST_TARGET " HTTP/1.1" CRLF, .status_code = 101},
	    {.start_line = "GET " REQUEST_TARGET " HTTP/1.0" CRLF, .status_code = 400},
	    {.start_line = "GET " REQUEST_TARGET " HTTP/2.0" CRLF, .status_code = 101},
	    {.start_line = "GET " REQUEST_TARGET " HTTP/1.2" CRLF, .status_code = 101},
	    {.start_line = "GET " REQUEST_TARGET CRLF, .status_code = 400},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(test_cases); i++) {
		setUp();

		struct http_test test_case = test_cases[i];

		uint8_t *close_frame;
		uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
		uint8_t data[2] = {0x3, 0xe8};
		bs_read_exactly_buffer_size = assemble_frame(WS_HEADER_FIN | CIO_WEBSOCKET_CLOSE_FRAME, mask, data, sizeof(data), &close_frame);

		bs_read_exactly_buffer = close_frame;

		bs_write_fake.custom_fake = bs_fake_write;
		bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Serving http failed!");

		struct cio_socket *s = server.alloc_client();

		//char start_line[100];
		//snprintf(start_line, sizeof(start_line) - 1, "GET " REQUEST_TARGET " HTTP/%d.%d" CRLF, test_case.major, test_case.minor);

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

		free(close_frame);
	}
}

struct protocol_test {
	const char *protocol_line;
	int status_code;
	cio_http_alloc_handler handler;
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
		setUp();

		struct protocol_test test_case = test_cases[i];

		uint8_t *close_frame;
		uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
		uint8_t data[2] = {0x3, 0xe8};
		bs_read_exactly_buffer_size = assemble_frame(WS_HEADER_FIN | CIO_WEBSOCKET_CLOSE_FRAME, mask, data, sizeof(data), &close_frame);

		bs_read_exactly_buffer = close_frame;

		bs_write_fake.custom_fake = bs_fake_write;
		bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, test_case.handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
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

		free(close_frame);
	}
}

static void test_ws_location_wrong_http_method(void)
{
	bs_read_exactly_buffer_size = 0;

	bs_read_exactly_buffer = NULL;

	bs_write_fake.custom_fake = bs_fake_write;
	bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
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
		setUp();
		bs_read_exactly_buffer_size = 0;

		bs_read_exactly_buffer = NULL;

		bs_write_fake.custom_fake = bs_fake_write;
		bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
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
	}
}

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
		setUp();

		uint8_t *close_frame;
		uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
		uint8_t data[2] = {0x3, 0xe8};
		bs_read_exactly_buffer_size = assemble_frame(WS_HEADER_FIN | CIO_WEBSOCKET_CLOSE_FRAME, mask, data, sizeof(data), &close_frame);

		bs_read_exactly_buffer = close_frame;

		bs_write_fake.custom_fake = bs_fake_write;
		bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
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
		free(close_frame);
	}
}

static void test_ws_location_no_upgrade(void)
{
	bs_read_exactly_buffer_size = 0;

	bs_read_exactly_buffer = NULL;

	bs_write_fake.custom_fake = bs_fake_write;
	bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
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
		setUp();

		uint8_t *close_frame;
		uint8_t mask[4] = {0x1, 0x2, 0x3, 0x4};
		uint8_t data[2] = {0x3, 0xe8};
		bs_read_exactly_buffer_size = assemble_frame(WS_HEADER_FIN | CIO_WEBSOCKET_CLOSE_FRAME, mask, data, sizeof(data), &close_frame);

		bs_read_exactly_buffer = close_frame;

		bs_write_fake.custom_fake = bs_fake_write;
		bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

		struct cio_http_server server;
		enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

		struct cio_http_location target;
		err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

		err = server.register_location(&server, &target);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

		err = server.serve(&server);
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
		free(close_frame);
	}
}

static void test_ws_location_wrong_key_length(void)
{
	bs_read_exactly_buffer_size = 0;

	bs_read_exactly_buffer = NULL;

	bs_write_fake.custom_fake = bs_fake_write;
	bs_read_exactly_fake.custom_fake = bs_read_exactly_from_buffer;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Server initialization failed!");

	struct cio_http_location target;
	err = cio_http_location_init(&target, REQUEST_TARGET, NULL, alloc_websocket_handler);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Request target initialization failed!");

	err = server.register_location(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Register request target failed!");

	err = server.serve(&server);
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

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_ws_location_close_in_onconnect);
	RUN_TEST(test_ws_location_wrong_http_version);
	RUN_TEST(test_ws_location_wrong_http_method);
	RUN_TEST(test_ws_location_wrong_ws_version);
	RUN_TEST(test_ws_version);
	RUN_TEST(test_ws_location_no_upgrade);
	RUN_TEST(test_ws_key);
	RUN_TEST(test_ws_location_wrong_key_length);
	RUN_TEST(test_ws_location_subprotocols);
	RUN_TEST(test_ws_location_write_error);
	return UNITY_END();
}
