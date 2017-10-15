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

#undef container_of
#define container_of(ptr, type, member) ( \
	(void *)((char *)ptr - offsetof(type, member)))

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(enum cio_error, cio_server_socket_init, struct cio_server_socket *, struct cio_eventloop *, unsigned int, cio_alloc_client, cio_free_client, cio_server_socket_close_hook)

struct dummy_client {
	struct cio_socket socket;
};

static struct cio_socket *alloc_dummy_client(void)
{
	struct dummy_client *dc = malloc(sizeof(*dc));
	memset(dc, 0xaf, sizeof(*dc));
	return &dc->socket;
}

static void free_dummy_client(struct cio_socket *socket)
{
	struct dummy_client *dc = container_of(socket, struct dummy_client, socket);
	free(dc);
}

static struct cio_eventloop loop;

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

void setUp(void)
{
	FFF_RESET_HISTORY();
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_server_init_correctly);
	RUN_TEST(test_server_init_no_server);
	RUN_TEST(test_server_init_no_loop);
	RUN_TEST(test_server_init_no_alloc);
	RUN_TEST(test_server_init_no_free);
	return UNITY_END();
}
