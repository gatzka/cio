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
#include "cio_write_buffer.h"

#undef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#undef container_of
#define container_of(ptr, type, member) ( \
	(void *)((char *)ptr - offsetof(type, member)))

struct memory_stream {
	struct cio_socket *socket;
	struct cio_io_stream ios;
	void *mem;
	size_t read_pos;
	size_t size;
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
	return cio_success;
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
		handler->handler.on_url = NULL;
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

static enum cio_error mem_close(struct cio_io_stream *io_stream)
{
	(void)io_stream;
	free_dummy_client(ms.socket);

	free(ms.mem);
	return cio_success;
}

static void memory_stream_init(struct memory_stream *stream, const char *fill_pattern, struct cio_socket *s)
{
	stream->socket = s;
	stream->read_pos = 0;
	stream->size = strlen(fill_pattern);
	stream->ios.read_some = read_some_max;
	stream->ios.write_some = NULL;
	stream->ios.close = mem_close;
	stream->mem = malloc(stream->size + 1);
	memset(stream->mem, 0x00, stream->size + 1);
	strncpy(ms.mem, fill_pattern, ms.size);
}

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(socket_set_reuse_address);
	RESET_FAKE(socket_accept);
	RESET_FAKE(socket_bind);
	RESET_FAKE(socket_close);
	RESET_FAKE(header_complete);
}

static void test_server_init_correctly(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Initialization failed!");
}

static void test_server_init_no_server(void)
{
	enum cio_error err = cio_http_server_init(NULL, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_server_init_no_loop(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, NULL, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_server_init_no_alloc(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, NULL, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_server_init_no_free(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_request_target_init_correctly(void)
{
	struct cio_http_request_target target;
	enum cio_error err = cio_http_request_target_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Initialization failed!");
}

static void test_request_target_init_no_target(void)
{
	enum cio_error err = cio_http_request_target_init(NULL, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_request_target_init_no_request_target(void)
{
	struct cio_http_request_target target;
	enum cio_error err = cio_http_request_target_init(&target, NULL, NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_request_target_init_no_alloc_handler(void)
{
	struct cio_http_request_target target;
	enum cio_error err = cio_http_request_target_init(&target, "/foo", NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Initialization did not fail!");
}

static void test_register_request_target_correctly(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_request_target target;
	err = cio_http_request_target_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_target(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");
}

static void test_register_request_target_no_server(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_request_target target;
	err = cio_http_request_target_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_target(NULL, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Register request target did not fail!");
}

static void test_register_request_target_no_target(void)
{
	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	err = server.register_target(&server, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(cio_invalid_argument, err, "Register request target did not fail!");
}

static void test_serve_correctly(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_accept_fake.custom_fake = accept_save_handler;

	header_complete_fake.custom_fake = header_complete_close;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_request_target target;
	err = cio_http_request_target_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_target(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Serving http failed!");

	struct cio_socket *s = server.alloc_client();
	const char request[] = "GET /foo HTTP/1.1\r\n\r\n";
	memory_stream_init(&ms, request, s);

	server.server_socket.handler(&server.server_socket, server.server_socket.handler_context, cio_success, s);
	TEST_ASSERT_EQUAL_MESSAGE(1, header_complete_fake.call_count, "header_complete was not called!");
}

static void test_serve_init_fails(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_fails;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_request_target target;
	err = cio_http_request_target_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_target(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(cio_success, err, "Serving http did not fail!");
}

static void test_serve_init_fails_reuse_address(void)
{
	cio_server_socket_init_fake.custom_fake = cio_server_socket_init_ok;
	socket_set_reuse_address_fake.return_val = cio_invalid_argument;

	struct cio_http_server server;
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_request_target target;
	err = cio_http_request_target_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_target(&server, &target);
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
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_request_target target;
	err = cio_http_request_target_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_target(&server, &target);
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
	enum cio_error err = cio_http_server_init(&server, 8080, &loop, alloc_dummy_client, free_dummy_client);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Server initialization failed!");

	struct cio_http_request_target target;
	err = cio_http_request_target_init(&target, "/foo", NULL, alloc_dummy_handler);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Request target initialization failed!");

	err = server.register_target(&server, &target);
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, err, "Register request target failed!");

	err = server.serve(&server);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(cio_success, err, "Serving http did not fail!");

	TEST_ASSERT_EQUAL_MESSAGE(1, socket_close_fake.call_count, "Close was not called!");
}


int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_server_init_correctly);
	RUN_TEST(test_server_init_no_server);
	RUN_TEST(test_server_init_no_loop);
	RUN_TEST(test_server_init_no_alloc);
	RUN_TEST(test_server_init_no_free);
	RUN_TEST(test_request_target_init_correctly);
	RUN_TEST(test_request_target_init_no_alloc_handler);
	RUN_TEST(test_request_target_init_no_request_target);
	RUN_TEST(test_request_target_init_no_target);
	RUN_TEST(test_register_request_target_correctly);
	RUN_TEST(test_register_request_target_no_server);
	RUN_TEST(test_register_request_target_no_target);
	RUN_TEST(test_serve_correctly);
	RUN_TEST(test_serve_init_fails);
	RUN_TEST(test_serve_init_fails_reuse_address);
	RUN_TEST(test_serve_init_fails_bind);
	RUN_TEST(test_serve_init_fails_accept);
	return UNITY_END();
}
