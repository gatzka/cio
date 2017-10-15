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

#undef container_of
#define container_of(ptr, type, member) ( \
	(void *)((char *)ptr - offsetof(type, member)))

struct dummy_handler {
	struct cio_http_request_handler handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
};

static const size_t read_buffer_size = 200;

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_init, struct cio_server_socket *, struct cio_eventloop *, unsigned int, cio_alloc_client, cio_free_client, cio_server_socket_close_hook)

static struct cio_socket *alloc_dummy_client(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	memset(client, 0xaf, sizeof(*client));
	return &client->socket;
}

static void free_dummy_client(struct cio_socket *socket)
{
	struct cio_http_client *client = container_of(socket, struct cio_http_client, socket);
	free(client);
}

static void free_dummy_handler(struct cio_http_request_handler *handler)
{
	struct dummy_handler *dh = container_of(handler, struct dummy_handler, handler);
	free(dh);
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
		handler->handler.on_headers_complete = NULL;
		return &handler->handler;
	}
}

static struct cio_eventloop loop;

void setUp(void)
{
	FFF_RESET_HISTORY();
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
	return UNITY_END();
}
