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
#include "cio_util.h"
#include "cio_websocket_location_handler.h"
#include "cio_write_buffer.h"

static struct cio_eventloop loop;
static struct cio_http_server http_server;

static const uint16_t AUTOBAHN_SERVER_PORT = 9001;

#define read_buffer_size (1024)

static const uint64_t header_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t body_read_timeout = UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
static const uint64_t response_timeout = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

struct ws_autobahn_handler {
	struct cio_websocket_location_handler ws_handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb_message;
	size_t echo_write_index;
	bool start_new_write_chunk;
	uint8_t echo_buffer[read_buffer_size];
};

static void free_autobahn_handler(struct cio_websocket_location_handler *wslh)
{
	struct ws_autobahn_handler *h = cio_container_of(wslh, struct ws_autobahn_handler, ws_handler);
	free(h);
}

static void read_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err, size_t frame_length, uint8_t *data, size_t length, bool last_chunk, bool last_frame, bool is_binary);

static void write_complete(struct cio_websocket *ws, void *handler_context, enum cio_error err)
{
	(void)handler_context;
	if (err == CIO_SUCCESS) {
		err = ws->read_message(ws, read_handler, NULL);
		if (err != CIO_SUCCESS) {
			fprintf(stderr, "could not start reading a new message!\n");
		}
	}
}

static void read_handler(struct cio_websocket *ws, void *handler_context, enum cio_error err, size_t frame_length, uint8_t *data, size_t length, bool last_chunk, bool last_frame, bool is_binary)
{
	(void)handler_context;

	if (err == CIO_SUCCESS) {
		struct cio_websocket_location_handler *handler = cio_container_of(ws, struct cio_websocket_location_handler, websocket);
		struct ws_autobahn_handler *ah = cio_container_of(handler, struct ws_autobahn_handler, ws_handler);

		if (frame_length <= read_buffer_size) {
			memcpy(ah->echo_buffer + ah->echo_write_index, data, length);
			ah->echo_write_index +=length;
			if (last_chunk) {
				ah->echo_write_index = 0;
				cio_write_buffer_head_init(&ah->wbh);
				cio_write_buffer_const_element_init(&ah->wb_message, ah->echo_buffer, frame_length);
				cio_write_buffer_queue_tail(&ah->wbh, &ah->wb_message);
				ws->write_message_first_chunk(ws, cio_write_buffer_get_length(&ah->wbh), &ah->wbh, last_frame, is_binary, write_complete, NULL);
			} else {
				err = ws->read_message(ws, read_handler, NULL);
			}
		} else {
			// make chunked transfers
			cio_write_buffer_head_init(&ah->wbh);
			cio_write_buffer_const_element_init(&ah->wb_message, data, length);
			cio_write_buffer_queue_tail(&ah->wbh, &ah->wb_message);
			if (ah->start_new_write_chunk) {
				err = ws->write_message_first_chunk(ws, frame_length, &ah->wbh, last_frame, is_binary, write_complete, NULL);
			} else {
				err = ws->write_message_continuation_chunk(ws, &ah->wbh, write_complete, NULL);
			}

			ah->start_new_write_chunk = last_chunk;
		}

		if (err != CIO_SUCCESS) {
			fprintf(stderr, "could not start writing message!\n");
		}

	} else if (err != CIO_EOF) {
		fprintf(stderr, "read failure!\n");
	}
}

static void on_connect(struct cio_websocket *ws)
{
	enum cio_error err = ws->read_message(ws, read_handler, NULL);
	if (err != CIO_SUCCESS) {
		fprintf(stderr, "could not start reading a new message!\n");
	}
}

static void on_error(const struct cio_websocket *ws, enum cio_error err, const char *reason)
{
	(void)ws;
	fprintf(stderr, "Unexpected error: %d, %s\n", err, reason);
}

static struct cio_http_location_handler *alloc_autobahn_handler(const void *config)
{
	(void)config;
	struct ws_autobahn_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	}

	cio_websocket_location_handler_init(&handler->ws_handler, NULL, 0, on_connect, free_autobahn_handler);
	handler->ws_handler.websocket.on_error = on_error;
	handler->echo_write_index = 0;
	handler->start_new_write_chunk = true;
	return &handler->ws_handler.http_location;
}

static struct cio_socket *alloc_http_client(void)
{
	struct cio_http_client *client = malloc(sizeof(*client) + read_buffer_size);
	if (cio_unlikely(client == NULL)) {
		return NULL;
	}

	client->buffer_size = read_buffer_size;
	return &client->socket;
}

static void free_http_client(struct cio_socket *socket)
{
	struct cio_http_client *client = cio_container_of(socket, struct cio_http_client, socket);
	free(client);
}

static void http_server_closed(struct cio_http_server *s)
{
	(void)s;
	cio_eventloop_cancel(&loop);
}

static void sighandler(int signum)
{
	(void)signum;
	http_server.shutdown(&http_server, http_server_closed);
}

static void serve_error(struct cio_http_server *server, const char *reason)
{
	(void)reason;
	server->shutdown(server, http_server_closed);
}

int main(void)
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

	err = cio_http_server_init(&http_server, AUTOBAHN_SERVER_PORT, &loop, serve_error, header_read_timeout, body_read_timeout, response_timeout, alloc_http_client, free_http_client);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	struct cio_http_location autobahn_target;
	cio_http_location_init(&autobahn_target, "/", NULL, alloc_autobahn_handler);
	http_server.register_location(&http_server, &autobahn_target);

	err = http_server.serve(&http_server);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	cio_eventloop_run(&loop);

destroy_loop:
	cio_eventloop_destroy(&loop);
	return ret;
}
