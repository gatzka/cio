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
#include "cio_timer.h"
#include "cio_write_buffer.h"
#include "cio_websocket_location_handler.h"
#include "cio_util.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static struct cio_eventloop loop;

static const size_t read_buffer_size = 2000;

static const uint64_t read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t ping_period_ns = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

struct ws_echo_handler {
	struct cio_websocket_location_handler ws_handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb_message;
	struct cio_write_buffer wb_ping_message;
	struct cio_timer ping_timer;
};

static void free_websocket_handler(struct cio_http_location_handler *handler)
{
	struct ws_echo_handler *h = container_of(handler, struct ws_echo_handler, ws_handler);
	h->ping_timer.close(&h->ping_timer);
	free(h);
}

static void send_ping(struct cio_timer *timer, void *handler_context, enum cio_error err);

static void ping_written(struct cio_websocket *ws, void *handler_context, enum cio_error err)
{
	if (err != CIO_SUCCESS) {
		fprintf(stderr, "writing ping frame failed!\n");
		ws->close(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, NULL);
	} else {
		struct cio_timer *timer = (struct cio_timer *)handler_context;
		err = timer->expires_from_now(timer, ping_period_ns, send_ping, ws);
		if (err != CIO_SUCCESS) {
			fprintf(stderr, "Could not start ping timer!\n");
			ws->close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL);
		}
	}
}

static void send_ping(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	struct cio_websocket *ws = (struct cio_websocket *)handler_context;
	if (err == CIO_SUCCESS) {
		fprintf(stdout, "Sending ping!\n");

		struct cio_websocket_location_handler *handler = container_of(ws, struct cio_websocket_location_handler, websocket);
		struct ws_echo_handler *eh = container_of(handler, struct ws_echo_handler, ws_handler);

		struct cio_write_buffer wbh;
		cio_write_buffer_head_init(&wbh);
		static const char *ping_message = "ping";
		cio_write_buffer_const_element_init(&eh->wb_ping_message, ping_message, strlen(ping_message));
		cio_write_buffer_queue_head(&wbh, &eh->wb_ping_message);
		ws->write_pingframe(ws, &wbh, ping_written, timer);
	} else if (err != CIO_OPERATION_ABORTED){
		fprintf(stderr, "ping timer failed!\n");
		ws->close(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, NULL);
	}
}

static void write_complete(struct cio_websocket *ws, void *handler_context, enum cio_error err)
{
	(void)handler_context;

	if (err == CIO_SUCCESS) {
		static const char *close_message = "Good Bye!";
		ws->close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, close_message);
	} else {
		ws->close(ws, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "write did not complete");
	}
}

static void print_payload(const uint8_t *data, size_t length)
{
	if (length > 0) {
		fwrite(data, length, 1, stdout);
		fflush(stdout);
		fprintf(stdout, "\n");
	} else {
		fprintf(stdout, "no payload\n");
	}
}

static void on_control(const struct cio_websocket *ws, enum cio_websocket_frame_type type, const uint8_t *data, size_t length)
{
	(void)ws;

	switch (type) {
		case CIO_WEBSOCKET_CLOSE_FRAME:
			fprintf(stdout, "Got close frame: ");
			print_payload(data, length);
			break;

		case CIO_WEBSOCKET_PING_FRAME:
			fprintf(stdout, "Got ping frame: ");
			print_payload(data, length);
			break;

		case CIO_WEBSOCKET_PONG_FRAME:
			fprintf(stdout, "Got pong frame: ");
			print_payload(data, length);
			break;

		default:
			fprintf(stderr, "Got unknown control frame: %x\n", type);
			break;
	}
}

static void read_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err, uint8_t *data, size_t length, bool last_frame, bool is_binary)
{
	(void)handler_context;
	if (err == CIO_SUCCESS) {
		struct cio_websocket_location_handler *handler = container_of(ws, struct cio_websocket_location_handler, websocket);
		struct ws_echo_handler *eh = container_of(handler, struct ws_echo_handler, ws_handler);

		fprintf(stdout, "Got text message (last frame: %s):", last_frame ? "true" : "false");
		fwrite(data, length, 1, stdout);
		fflush(stdout);
		fprintf(stdout, "\n");

		static const char *text_message = "Hello World!";
		cio_write_buffer_head_init(&eh->wbh);
		cio_write_buffer_const_element_init(&eh->wb_message, text_message, strlen(text_message));
		cio_write_buffer_queue_tail(&eh->wbh, &eh->wb_message);
		err = ws->write_message(ws, &eh->wbh, true, is_binary, write_complete, NULL);
		if (err != CIO_SUCCESS) {
			fprintf(stderr, "Could not start writing message!\n");
		}
	} else if (err != CIO_EOF) {
		fprintf(stderr, "read failure!\n");
	}
}

static void on_connect(struct cio_websocket *ws)
{
	fprintf(stdout, "Websocket connected!\n");

	struct cio_websocket_location_handler *handler = container_of(ws, struct cio_websocket_location_handler, websocket);
	struct ws_echo_handler *eh = container_of(handler, struct ws_echo_handler, ws_handler);
	enum cio_error err = cio_timer_init(&eh->ping_timer, &loop, NULL);
	if (err != CIO_SUCCESS) {
		fprintf(stderr, "Could not initialize ping timer!\n");
		ws->close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL);
		return;
	}

	err = eh->ping_timer.expires_from_now(&eh->ping_timer, ping_period_ns, send_ping, ws);
	if (err != CIO_SUCCESS) {
		fprintf(stderr, "Could not start ping timer!\n");
		ws->close(ws, CIO_WEBSOCKET_CLOSE_NORMAL, NULL);
		return;
	}

	err = ws->read_message(ws, read_handler, NULL);
	if (err != CIO_SUCCESS) {
		fprintf(stderr, "Could not start reading a new message!\n");
	}
}

static struct cio_http_location_handler *alloc_websocket_handler(const void *config)
{
	(void)config;
	struct ws_echo_handler *handler = malloc(sizeof(*handler));
	if (unlikely(handler == NULL)) {
		return NULL;
	} else {
		static const char *subprotocols[2] = {"echo", "jet"};
		cio_websocket_location_handler_init(&handler->ws_handler, subprotocols, ARRAY_SIZE(subprotocols), on_connect);
		handler->ws_handler.websocket.on_control = on_control;
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

static void serve_error(struct cio_http_server *server, const char *reason)
{
	fprintf(stderr, "http server error: %s\n", reason);
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
