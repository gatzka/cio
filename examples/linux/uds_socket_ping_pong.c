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

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_io_stream.h"
#include "cio_read_buffer.h"
#include "cio_server_socket.h"
#include "cio_socket.h"
#include "cio_socket_address.h"
#include "cio_util.h"
#include "cio_write_buffer.h"
#include "platform/linux/cio_unix_address.h"

static unsigned long long max_pings;
static struct cio_eventloop loop;

static const char HELLO[] = "Hello";
static const uint64_t CLOSE_TIMEOUT_NS = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
enum { BASE_10 = 10 };
enum { SERVERSOCKET_BACKLOG = 5 };
enum { BUFFER_SIZE = 128 };

struct echo_client {
	size_t bytes_read;
	struct cio_buffered_stream buffered_stream;
	struct cio_socket socket;
	struct cio_write_buffer wb;
	struct cio_write_buffer wbh;
	struct cio_read_buffer rb;
	uint8_t buffer[BUFFER_SIZE];
};

struct client {
	size_t bytes_read;
	unsigned long long number_of_pings;
	struct cio_buffered_stream buffered_stream;
	struct cio_write_buffer wb;
	struct cio_write_buffer wbh;
	struct cio_read_buffer rb;
	uint8_t buffer[BUFFER_SIZE];
};

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static struct cio_socket *alloc_echo_client(void)
{
	struct echo_client *client = malloc(sizeof(*client));
	if (cio_unlikely(client == NULL)) {
		return NULL;
	}

	client->bytes_read = 0;
	return &client->socket;
}

static void free_echo_client(struct cio_socket *socket)
{
	fprintf(stdout, "client will be freed\n");
	struct echo_client *client = cio_container_of(socket, struct echo_client, socket);
	free(client);
}

static void client_socket_close_hook(struct cio_socket *socket)
{
	(void)socket;
	fprintf(stdout, "connection closed by server peer\n");
	cio_eventloop_cancel(&loop);
}

static void server_handle_read(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t num_bytes);

static void server_handle_write(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	struct echo_client *client = handler_context;

	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "server failed to write!\n");
		return;
	}

	cio_read_buffer_consume(&client->rb, client->bytes_read);

	err = cio_buffered_stream_read_at_least(bs, &client->rb, sizeof(HELLO), server_handle_read, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "server could no start reading!\n");
		return;
	}
}

static void server_handle_read(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t num_bytes)
{
	struct echo_client *client = handler_context;

	if (cio_unlikely(err == CIO_EOF)) {
		fprintf(stdout, "connection closed by client peer\n");
		cio_buffered_stream_close(bs);
		return;
	}

	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "read error!\n");
		cio_buffered_stream_close(bs);
		return;
	}

	client->bytes_read = num_bytes;
	cio_write_buffer_head_init(&client->wbh);
	cio_write_buffer_element_init(&client->wb, cio_read_buffer_get_read_ptr(read_buffer), num_bytes);
	cio_write_buffer_queue_tail(&client->wbh, &client->wb);
	cio_buffered_stream_write(bs, &client->wbh, server_handle_write, client);
}

static void handle_accept(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)handler_context;

	struct echo_client *client = cio_container_of(socket, struct echo_client, socket);

	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "accept error!\n");
		goto error;
	}

	err = cio_read_buffer_init(&client->rb, client->buffer, sizeof(client->buffer));
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "failed to init read buffer!\n");
		goto error;
	}

	struct cio_io_stream *stream = cio_socket_get_io_stream(socket);

	struct cio_buffered_stream *bs = &client->buffered_stream;
	err = cio_buffered_stream_init(bs, stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "failed to init buffered stream!\n");
		goto error;
	}

	err = cio_buffered_stream_read_at_least(bs, &client->rb, sizeof(HELLO), server_handle_read, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "server could no start reading!\n");
		goto error;
	}

	return;

error:
	cio_server_socket_close(ss);
	cio_eventloop_cancel(ss->impl.loop);
}

static void client_handle_write(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err);

static void client_handle_read(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t num_bytes)
{
	struct client *client = handler_context;

	if (cio_unlikely(err == CIO_EOF)) {
		fprintf(stdout, "connection closed by server peer\n");
		cio_buffered_stream_close(bs);
		return;
	}

	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "read error!\n");
		cio_buffered_stream_close(bs);
		return;
	}

	client->number_of_pings++;
	if (cio_likely(client->number_of_pings < max_pings)) {
		client->bytes_read = num_bytes;
		cio_write_buffer_head_init(&client->wbh);
		cio_write_buffer_element_init(&client->wb, cio_read_buffer_get_read_ptr(read_buffer), num_bytes);
		cio_write_buffer_queue_tail(&client->wbh, &client->wb);
		cio_buffered_stream_write(bs, &client->wbh, client_handle_write, client);
	} else {
		fprintf(stdout, "Clients closes socket\n");
		cio_buffered_stream_close(bs);
	}
}

static void client_handle_write(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "client write failed!\n");
		cio_buffered_stream_close(bs);
		return;
	}

	struct client *client = (struct client *)handler_context;
	cio_read_buffer_consume(&client->rb, client->bytes_read);
	err = cio_buffered_stream_read_at_least(bs, &client->rb, sizeof(HELLO), client_handle_read, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "client could no start reading!\n");
		cio_buffered_stream_close(bs);
	}
}

static void handle_connect(struct cio_socket *socket, void *handler_context, enum cio_error err)
{
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Connect failed!\n");
		cio_socket_close(socket);
		return;
	}

	struct client *client = (struct client *)handler_context;
	cio_read_buffer_init(&client->rb, client->buffer, sizeof(client->buffer));
	struct cio_buffered_stream *bs = &client->buffered_stream;
	cio_buffered_stream_init(bs, cio_socket_get_io_stream(socket));

	cio_write_buffer_head_init(&client->wbh);
	cio_write_buffer_const_element_init(&client->wb, HELLO, sizeof(HELLO));
	cio_write_buffer_queue_tail(&client->wbh, &client->wb);
	err = cio_buffered_stream_write(bs, &client->wbh, client_handle_write, client);

	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "client write failed!\n");
		cio_socket_close(socket);
		return;
	}
}

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s <number of messages to be exchanged>\n", name);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	max_pings = strtoul(argv[1], NULL, BASE_10);
	if (max_pings == ULLONG_MAX) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	int ret = EXIT_SUCCESS;
	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		fprintf(stderr, "could no install SIGTERM handler!\n");
		return -1;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		fprintf(stderr, "could no install SIGINT handler!\n");
		signal(SIGTERM, SIG_DFL);
		return -1;
	}

	struct cio_socket_address endpoint;
	const char path[] = {"\0/tmp/foobar"};
	enum cio_error err = cio_init_uds_socket_address(&endpoint, path);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "could no init server socket endpoint!\n");
		return -1;
	}

	err = cio_eventloop_init(&loop);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return EXIT_FAILURE;
	}

	struct cio_server_socket ss;
	err = cio_server_socket_init(&ss, &loop, SERVERSOCKET_BACKLOG, cio_socket_address_get_family(&endpoint), alloc_echo_client, free_echo_client, CLOSE_TIMEOUT_NS, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "could not init server socket!\n");
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	err = cio_server_socket_set_reuse_address(&ss, true);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "could not set reuse_address for server socket!\n");
		ret = EXIT_FAILURE;
		goto close_server_socket;
	}

	err = cio_server_socket_bind(&ss, &endpoint);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "could not bind server socket!\n");
		ret = EXIT_FAILURE;
		goto close_server_socket;
	}

	err = cio_server_socket_accept(&ss, handle_accept, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "could not run accept on server socket!\n");
		ret = EXIT_FAILURE;
		goto close_server_socket;
	}

	struct cio_socket_address client_endpoint;
	err = cio_init_uds_socket_address(&client_endpoint, path);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "could not init client socket endpoint!\n");
		ret = EXIT_FAILURE;
		goto close_server_socket;
	}

	struct cio_socket socket;
	err = cio_socket_init(&socket, cio_socket_address_get_family(&client_endpoint), &loop, CLOSE_TIMEOUT_NS, client_socket_close_hook);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "could not init client socket endpoint!\n");
		ret = EXIT_FAILURE;
		goto close_server_socket;
	}

	struct client client;
	client.bytes_read = 0;
	client.number_of_pings = 0;

	err = cio_socket_connect(&socket, &client_endpoint, handle_connect, &client);
	if (err != CIO_SUCCESS) {
		fprintf(stderr, "could not connect to server!\n");
		ret = EXIT_FAILURE;
		goto close_server_socket;
	}

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
	}

close_server_socket:
	cio_server_socket_close(&ss);
destroy_loop:
	cio_eventloop_destroy(&loop);
	return ret;
}
