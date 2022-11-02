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

#include "cio/buffered_stream.h"
#include "cio/error_code.h"
#include "cio/eventloop.h"
#include "cio/inet_address.h"
#include "cio/io_stream.h"
#include "cio/read_buffer.h"
#include "cio/server_socket.h"
#include "cio/socket.h"
#include "cio/socket_address.h"
#include "cio/util.h"
#include "cio/write_buffer.h"

static struct cio_eventloop loop;
enum { SERVERSOCKET_BACKLOG = 5 };
enum { SERVERSOCKET_LISTEN_PORT = 12345 };
static const uint64_t CLOSE_TIMEOUT_NS = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
enum { BUFFER_SIZE = 128 };
enum { IPV6_ADDRESS_SIZE = 16 };

struct echo_client {
	size_t number_of_bytes_read;
	struct cio_buffered_stream buffered_stream;
	struct cio_socket socket;
	struct cio_write_buffer wb;
	struct cio_write_buffer wbh;
	struct cio_read_buffer rb;
	uint8_t buffer[BUFFER_SIZE];
};

static struct cio_socket *alloc_echo_client(void)
{
	struct echo_client *client = malloc(sizeof(*client));
	if (cio_unlikely(client == NULL)) {
		return NULL;
	}

	cio_read_buffer_init(&client->rb, client->buffer, sizeof(client->buffer));

	return &client->socket;
}

static void free_echo_client(struct cio_socket *socket)
{
	struct echo_client *client = cio_container_of(socket, struct echo_client, socket);
	free(client);
}

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer);

static void handle_write(struct cio_buffered_stream *buffered_stream, void *handler_context, enum cio_error err)
{
	(void)buffered_stream;
	struct echo_client *client = handler_context;

	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "write error!\n");
		return;
	}

	cio_read_buffer_consume(&client->rb, client->number_of_bytes_read);
	struct cio_io_stream *stream = cio_socket_get_io_stream(&client->socket);
	stream->read_some(stream, &client->rb, handle_read, client);
}

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer)
{
	struct echo_client *client = handler_context;
	if (err == CIO_EOF) {
		(void)fprintf(stdout, "connection close by peer\n");
		stream->close(stream);
		return;
	}

	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "read error!\n");
		return;
	}

	cio_write_buffer_head_init(&client->wbh);
	size_t number_of_unread_bytes = cio_read_buffer_unread_bytes(read_buffer);
	client->number_of_bytes_read = number_of_unread_bytes;
	cio_write_buffer_element_init(&client->wb, cio_read_buffer_get_read_ptr(read_buffer), number_of_unread_bytes);
	cio_write_buffer_queue_tail(&client->wbh, &client->wb);
	cio_buffered_stream_write(&client->buffered_stream, &client->wbh, handle_write, client);
}

static void handle_accept(struct cio_server_socket *server_socket, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)handler_context;

	struct echo_client *client = cio_container_of(socket, struct echo_client, socket);

	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "accept error!\n");
		cio_server_socket_close(server_socket);
		cio_eventloop_cancel(server_socket->impl.loop);
		return;
	}

	struct cio_io_stream *stream = cio_socket_get_io_stream(socket);
	err = cio_buffered_stream_init(&client->buffered_stream, stream);
	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "could not init buffered stream!\n");
		cio_server_socket_close(server_socket);
		cio_eventloop_cancel(server_socket->impl.loop);
		return;
	}

	stream->read_some(stream, &client->rb, handle_read, client);
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

	struct cio_socket_address endpoint;
	enum cio_error err = cio_init_inet_socket_address(&endpoint, cio_get_inet_address_any4(), SERVERSOCKET_LISTEN_PORT);
	if (err != CIO_SUCCESS) {
		return -1;
	}

	err = cio_eventloop_init(&loop);
	if (err != CIO_SUCCESS) {
		return EXIT_FAILURE;
	}

	struct cio_server_socket server_socket;
	err = cio_server_socket_init(&server_socket, &loop, SERVERSOCKET_BACKLOG, cio_socket_address_get_family(&endpoint), alloc_echo_client, free_echo_client, CLOSE_TIMEOUT_NS, NULL);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	err = cio_server_socket_set_tcp_fast_open(&server_socket, true);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "could not set TCP FASTOPEN for server socket!\n");
		ret = EXIT_FAILURE;
		goto close_socket;
	}

	err = cio_server_socket_set_reuse_address(&server_socket, true);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto close_socket;
	}

	err = cio_server_socket_bind(&server_socket, &endpoint);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto close_socket;
	}

	err = cio_server_socket_accept(&server_socket, handle_accept, NULL);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto close_socket;
	}

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
	}

close_socket:
	cio_server_socket_close(&server_socket);
destroy_loop:
	cio_eventloop_destroy(&loop);
	return ret;
}
