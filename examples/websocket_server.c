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
#include <string.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_http_location_handler.h"
#include "cio_http_server.h"
#include "cio_write_buffer.h"
#include "cio_websocket_location_handler.h"
#include "cio_util.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static struct cio_eventloop loop;

static const size_t read_buffer_size = 2000;

static const uint64_t read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);


struct ws_echo_handler {
	struct cio_websocket_location_handler ws_handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb_message;
};

static void free_websocket_handler(struct cio_http_location_handler *handler)
{
	struct ws_echo_handler *h = container_of(handler, struct ws_echo_handler, ws_handler);
	free(h);
}

static void onconnect_handler(struct cio_websocket *ws)
{
	(void)ws;
	fprintf(stdout, "Websocket connected!\n");
}

static void write_complete(struct cio_websocket *ws, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err)
{
	(void)handler_context;
	(void)buffer;
	(void)err;
	struct cio_websocket_location_handler *handler = container_of(ws, struct cio_websocket_location_handler, websocket);
	struct ws_echo_handler *eh = container_of(handler, struct ws_echo_handler, ws_handler);

	static const char *close_message = "Good Bye!";
	cio_write_buffer_head_init(&eh->wbh);
	cio_write_buffer_element_init(&eh->wb_message, close_message, strlen(close_message));
	cio_write_buffer_queue_tail(&eh->wbh, &eh->wb_message);
	ws->close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, &eh->wbh);
}

static void ontextframe_received(struct cio_websocket *ws, char *data, size_t length, bool last_frame)
{
	struct cio_websocket_location_handler *handler = container_of(ws, struct cio_websocket_location_handler, websocket);
	struct ws_echo_handler *eh = container_of(handler, struct ws_echo_handler, ws_handler);

	fprintf(stdout, "Got text message (last frame: %s):", last_frame ? "true" : "false");
	fwrite(data, length, 1, stdout);
	fflush(stdout);
	fprintf(stdout, "\n");

	static const char *text_message = "Hello World!";
	cio_write_buffer_head_init(&eh->wbh);
	cio_write_buffer_element_init(&eh->wb_message, text_message, strlen(text_message));
	cio_write_buffer_queue_tail(&eh->wbh, &eh->wb_message);
	ws->write_text_frame(ws, &eh->wbh, true, write_complete, NULL);
}

static struct cio_http_location_handler *alloc_websocket_handler(const void *config)
{
	(void)config;
	struct ws_echo_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		static const char *subprotocols[2] = {"echo", "jet"};
		cio_websocket_location_handler_init(&handler->ws_handler, subprotocols, ARRAY_SIZE(subprotocols));
		handler->ws_handler.websocket.onconnect_handler = onconnect_handler;
		handler->ws_handler.websocket.ontextframe_received = ontextframe_received;
		handler->ws_handler.http_location.free = free_websocket_handler;
		return &handler->ws_handler.http_location;
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
	if (err != CIO_SUCCESS) {
		return EXIT_FAILURE;
	}

	struct cio_http_server server;
	err = cio_http_server_init(&server, 8080, &loop, serve_error, read_timeout, alloc_http_client, free_http_client);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	struct cio_http_location target_foo;
	cio_http_location_init(&target_foo, "/ws", NULL, alloc_websocket_handler);
	server.register_location(&server, &target_foo);

	err = server.serve(&server);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	cio_eventloop_run(&loop);

destroy_loop:
	cio_eventloop_destroy(&loop);
	return ret;
}
