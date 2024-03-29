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
#include <string.h>

#include "cio/error_code.h"
#include "cio/eventloop.h"
#include "cio/http_location_handler.h"
#include "cio/http_server.h"
#include "cio/inet_address.h"
#include "cio/timer.h"
#include "cio/util.h"
#include "cio/websocket_location_handler.h"
#include "cio/write_buffer.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static struct cio_eventloop loop;
static struct cio_http_server http_server;

enum { READ_BUFFER_SIZE = 2000 };
enum { HTTPSERVER_LISTEN_PORT = 8080 };

static const uint64_t HEADER_READ_TIMEOUT = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t BODY_READ_TIMEOUT = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t RESPONSE_TIMEOUT = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t CLOSE_TIMEOUT_NS = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t PING_PERIOD_NS = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

struct ws_echo_handler {
	struct cio_websocket_location_handler ws_handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb_message;
	struct cio_write_buffer wb_ping_message;
	struct cio_timer ping_timer;
};

static void free_websocket_handler(struct cio_websocket_location_handler *wslh)
{
	struct ws_echo_handler *handler = cio_container_of(wslh, struct ws_echo_handler, ws_handler);
	cio_timer_close(&handler->ping_timer);
	free(handler);
}

static void send_ping(struct cio_timer *timer, void *handler_context, enum cio_error err);

static void ping_written(struct cio_websocket *websocket, void *handler_context, enum cio_error err)
{
	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "writing ping frame failed!\n");
		err = cio_websocket_close(websocket, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, NULL, NULL, NULL);
		if (err != CIO_SUCCESS) {
			(void)fprintf(stderr, "Could not start writing websocket close!\n");
		}
	} else {
		struct cio_timer *timer = (struct cio_timer *)handler_context;
		err = cio_timer_expires_from_now(timer, PING_PERIOD_NS, send_ping, websocket);
		if (err != CIO_SUCCESS) {
			(void)fprintf(stderr, "Could not start ping timer!\n");
			err = cio_websocket_close(websocket, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, NULL, NULL);
			if (err != CIO_SUCCESS) {
				(void)fprintf(stderr, "Could not start writing websocket close!\n");
			}
		}
	}
}

static void send_ping(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	struct cio_websocket *websocket = (struct cio_websocket *)handler_context;
	if (err == CIO_SUCCESS) {
		(void)fprintf(stdout, "Sending ping!\n");

		struct cio_websocket_location_handler *handler = cio_container_of(websocket, struct cio_websocket_location_handler, websocket);
		struct ws_echo_handler *echo_handler = cio_container_of(handler, struct ws_echo_handler, ws_handler);

		struct cio_write_buffer wbh;
		cio_write_buffer_head_init(&wbh);
		static const char *ping_message = "ping";
		cio_write_buffer_const_element_init(&echo_handler->wb_ping_message, ping_message, strlen(ping_message));
		cio_write_buffer_queue_head(&wbh, &echo_handler->wb_ping_message);
		err = cio_websocket_write_ping(websocket, &wbh, ping_written, timer);
		if (err != CIO_SUCCESS) {
			(void)fprintf(stderr, "Could not start writing websocket ping!\n");
		}
	} else if (err != CIO_OPERATION_ABORTED) {
		(void)fprintf(stderr, "ping timer failed!\n");
		err = cio_websocket_close(websocket, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, NULL, NULL, NULL);
		if (err != CIO_SUCCESS) {
			(void)fprintf(stderr, "Could not start writing websocket close!\n");
		}
	}
}

static void write_complete(struct cio_websocket *websocket, void *handler_context, enum cio_error err)
{
	(void)handler_context;

	if (err == CIO_SUCCESS) {
		static const char *close_message = "Good Bye!";
		err = cio_websocket_close(websocket, CIO_WEBSOCKET_CLOSE_NORMAL, close_message, NULL, NULL);
	} else {
		err = cio_websocket_close(websocket, CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR, "write did not complete", NULL, NULL);
	}

	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "Could not start writing websocket close!\n");
	}
}

static void print_payload(const uint8_t *data, uint_fast8_t length)
{
	if (length > 0) {
		(void)fwrite(data, length, 1, stdout);
		(void)fflush(stdout);
		(void)fprintf(stdout, "\n");
	} else {
		(void)fprintf(stdout, "no payload\n");
	}
}

static void on_control(const struct cio_websocket *websocket, enum cio_websocket_frame_type type, const uint8_t *data, uint_fast8_t length)
{
	(void)websocket;

	switch (type) {
	case CIO_WEBSOCKET_CLOSE_FRAME:
		(void)fprintf(stdout, "Got close frame: ");
		print_payload(data, length);
		break;

	case CIO_WEBSOCKET_PING_FRAME:
		(void)fprintf(stdout, "Got ping frame: ");
		print_payload(data, length);
		break;

	case CIO_WEBSOCKET_PONG_FRAME:
		(void)fprintf(stdout, "Got pong frame: ");
		print_payload(data, length);
		break;

	default:
		(void)fprintf(stderr, "Got unknown control frame: %x\n", type);
		break;
	}
}

static void read_handler(struct cio_websocket *websocket, void *handler_context, enum cio_error err, size_t frame_length, uint8_t *data, size_t chunk_length, bool last_chunk, bool last_frame, bool is_binary)
{
	(void)handler_context;
	(void)frame_length;
	(void)last_chunk;

	if (err == CIO_SUCCESS) {
		struct cio_websocket_location_handler *handler = cio_container_of(websocket, struct cio_websocket_location_handler, websocket);
		struct ws_echo_handler *echo_handler = cio_container_of(handler, struct ws_echo_handler, ws_handler);

		(void)fprintf(stdout, "Got text message (last frame: %s):", last_frame ? "true" : "false");
		(void)fwrite(data, chunk_length, 1, stdout);
		(void)fflush(stdout);
		(void)fprintf(stdout, "\n");

		static const char *text_message = "Hello World!";
		cio_write_buffer_head_init(&echo_handler->wbh);
		cio_write_buffer_const_element_init(&echo_handler->wb_message, text_message, strlen(text_message));
		cio_write_buffer_queue_tail(&echo_handler->wbh, &echo_handler->wb_message);
		err = cio_websocket_write_message_first_chunk(websocket, cio_write_buffer_get_total_size(&echo_handler->wbh), &echo_handler->wbh, true, is_binary, write_complete, NULL);
		if (err != CIO_SUCCESS) {
			(void)fprintf(stderr, "Could not start writing message!\n");
		}
	} else if (err != CIO_EOF) {
		(void)fprintf(stderr, "read failure!\n");
	}
}

static void on_connect(struct cio_websocket *websocket)
{
	(void)fprintf(stdout, "Websocket connected!\n");

	struct cio_websocket_location_handler *handler = cio_container_of(websocket, struct cio_websocket_location_handler, websocket);
	struct ws_echo_handler *echo_handler = cio_container_of(handler, struct ws_echo_handler, ws_handler);
	enum cio_error err = cio_timer_init(&echo_handler->ping_timer, &loop, NULL);
	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "Could not initialize ping timer!\n");
		err = cio_websocket_close(websocket, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, NULL, NULL);
		if (err != CIO_SUCCESS) {
			(void)fprintf(stderr, "Could not start writing websocket close!\n");
		}

		return;
	}

	err = cio_timer_expires_from_now(&echo_handler->ping_timer, PING_PERIOD_NS, send_ping, websocket);
	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "Could not start ping timer!\n");
		err = cio_websocket_close(websocket, CIO_WEBSOCKET_CLOSE_NORMAL, NULL, NULL, NULL);
		if (err != CIO_SUCCESS) {
			(void)fprintf(stderr, "Could not start writing websocket close!\n");
		}

		return;
	}

	err = cio_websocket_read_message(websocket, read_handler, NULL);
	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "Could not start reading a new message!\n");
	}
}

static struct cio_http_location_handler *alloc_websocket_handler(const void *config)
{
	(void)config;
	struct ws_echo_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	}

	static const char *subprotocols[2] = {"echo", "jet"};
	enum cio_error err = cio_websocket_location_handler_init(&handler->ws_handler, subprotocols, ARRAY_SIZE(subprotocols), on_connect, free_websocket_handler);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		free(handler);
		return NULL;
	}

	cio_websocket_set_on_control_cb(&handler->ws_handler.websocket, on_control);
	return &handler->ws_handler.http_location;
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
	(void)fprintf(stderr, "http server error: %s\n", reason);
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
	    .use_tcp_fastopen = false,
	    .alloc_client = alloc_http_client,
	    .free_client = free_http_client};

	err = cio_init_inet_socket_address(&config.endpoint, cio_get_inet_address_any6(), HTTPSERVER_LISTEN_PORT);
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
	cio_http_location_init(&target_foo, "/ws", NULL, alloc_websocket_handler);
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
