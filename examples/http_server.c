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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio/error_code.h"
#include "cio/eventloop.h"
#include "cio/http_client.h"
#include "cio/http_location_handler.h"
#include "cio/http_server.h"
#include "cio/util.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static struct cio_eventloop loop;
static struct cio_http_server http_server;

enum { HTTPSERVER_LISTEN_PORT = 8080 };
enum { READ_BUFFER_SIZE = 2000 };

static const uint64_t HEADER_READ_TIMEOUT = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t BODY_READ_TIMEOUT = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t RESPONSE_TIMEOUT = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t CLOSE_TIMEOUT_NS = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
enum { IPV6_ADDRESS_SIZE = 16 };

static const char DATA[] = "<html><body><h1>Hello, World!</h1></body></html>";

struct dummy_handler {
	struct cio_http_location_handler handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
};

static void free_dummy_handler(struct cio_http_location_handler *handler)
{
	struct dummy_handler *dummy_handler = cio_container_of(handler, struct dummy_handler, handler);
	free(dummy_handler);
}

static enum cio_http_cb_return dummy_on_message_complete(struct cio_http_client *client)
{
	struct cio_http_location_handler *handler = client->current_handler;
	struct dummy_handler *dummy_handler = cio_container_of(handler, struct dummy_handler, handler);
	cio_write_buffer_const_element_init(&dummy_handler->wb, DATA, sizeof(DATA) - 1);
	cio_write_buffer_queue_tail(&dummy_handler->wbh, &dummy_handler->wb);
	enum cio_error err = client->write_response(client, CIO_HTTP_STATUS_OK, &dummy_handler->wbh, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "writing response not allowed!");
		client->close(client);
	}

	return CIO_HTTP_CB_SUCCESS;
}

static struct cio_http_location_handler *alloc_dummy_handler(const void *config)
{
	(void)config;
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	}

	cio_http_location_handler_init(&handler->handler);
	cio_write_buffer_head_init(&handler->wbh);
	handler->handler.free = free_dummy_handler;
	handler->handler.on_message_complete = dummy_on_message_complete;
	return &handler->handler;
}

static struct cio_socket *alloc_http_client(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + READ_BUFFER_SIZE);
	if (cio_unlikely(client == NULL)) {
		return NULL;
	}

	client->buffer_size = READ_BUFFER_SIZE;
	return &client->socket;
}

static void free_http_client(struct cio_socket *socket)
{
	struct cio_http_client *client = cio_container_of(socket, struct cio_http_client, socket);
	free(client);
}

static void http_server_closed(const struct cio_http_server *server)
{
	(void)server;
	cio_eventloop_cancel(&loop);
}

static void sighandler(int signum)
{
	(void)signum;
	cio_http_server_shutdown(&http_server, http_server_closed);
}

static void serve_error(struct cio_http_server *server, const char *reason)
{
	(void)fprintf(stderr, "http server error: %server\n", reason);
	cio_http_server_shutdown(server, http_server_closed);
}

int main(void)
{
	int ret = EXIT_SUCCESS;
	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		return -1;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		(void)signal(SIGTERM, SIG_DFL);
		return -1;
	}

	enum cio_error err = cio_eventloop_init(&loop);
	if (err != CIO_SUCCESS) {
		return EXIT_FAILURE;
	}

	struct cio_http_server_configuration config = {
	    .on_error = serve_error,
	    .read_header_timeout_ns = HEADER_READ_TIMEOUT,
	    .read_body_timeout_ns = BODY_READ_TIMEOUT,
	    .response_timeout_ns = RESPONSE_TIMEOUT,
	    .close_timeout_ns = CLOSE_TIMEOUT_NS,
	    .use_tcp_fastopen = true,
	    .alloc_client = alloc_http_client,
	    .free_client = free_http_client};

	err = cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any4(), HTTPSERVER_LISTEN_PORT);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	err = cio_http_server_init(&http_server, &loop, &config);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	struct cio_http_location target_foo;
	cio_http_location_init(&target_foo, "/foo", NULL, alloc_dummy_handler);
	cio_http_server_register_location(&http_server, &target_foo);

	err = cio_http_server_serve(&http_server);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	cio_eventloop_run(&loop);

destroy_loop:
	cio_eventloop_destroy(&loop);
	return ret;
}
