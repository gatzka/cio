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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_http_request_handler.h"
#include "cio_http_server.h"
#include "cio_util.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static struct cio_eventloop loop;

static const size_t read_buffer_size = 2000;

static const uint64_t read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

static const char data[] = "<html><body><h1>Hello, World!</h1></body></html>";

struct dummy_handler {
	struct cio_http_request_handler handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
};

static void free_dummy_handler(struct cio_http_request_handler *handler)
{
	struct dummy_handler *dh = container_of(handler, struct dummy_handler, handler);
	free(dh);
}

static enum cio_http_cb_return dummy_on_message_complete(struct cio_http_client *client)
{
	struct cio_http_request_handler *handler = client->handler;
	struct dummy_handler *dh = container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_init(&dh->wb, data, sizeof(data));
	cio_write_buffer_queue_tail(&dh->wbh, &dh->wb);
	client->write_response(client, &dh->wbh);
	return cio_http_cb_success;
}

static enum cio_http_cb_return dummy_on_body(struct cio_http_client *client, const char *at, size_t length)
{
	(void)client;
	(void)at;
	(void)length;
	return cio_http_cb_success;
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
		handler->handler.on_body = dummy_on_body;
		handler->handler.on_message_complete = dummy_on_message_complete;
		return &handler->handler;
	}
}

static struct cio_socket *alloc_http_client(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	if (unlikely(client == NULL)) {
		return NULL;
	} else {
		client->buffer_size = read_buffer_size;
		return &client->socket;
	}
}

static void free_http_client(struct cio_socket *socket)
{
	struct cio_http_client *client = container_of(socket, struct cio_http_client, socket);
	free(client);
}

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void serve_error(struct cio_http_server *server)
{
	server->server_socket.close(&server->server_socket);
}

int main()
{
	int ret = EXIT_SUCCESS;
	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		return -1;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		signal(SIGTERM, SIG_DFL);
		return -1;
	}

	enum cio_error err = cio_eventloop_init(&loop);
	if (err != cio_success) {
		return EXIT_FAILURE;
	}

	struct cio_http_server server;
	err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_http_client, free_http_client);
	if (err != cio_success) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	struct cio_http_server_location target_foo;
	cio_http_server_location_init(&target_foo, "/foo", NULL, alloc_dummy_handler);
	server.register_location(&server, &target_foo);

	err = server.serve(&server);
	if (err != cio_success) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	cio_eventloop_run(&loop);

destroy_loop:
	cio_eventloop_destroy(&loop);
	return ret;
}
