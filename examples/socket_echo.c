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
#include "cio_io_stream.h"
#include "cio_read_buffer.h"
#include "cio_server_socket.h"
#include "cio_socket.h"
#include "cio_util.h"
#include "cio_write_buffer.h"

static struct cio_eventloop loop;
static const unsigned int SERVERSOCKET_BACKLOG = 5;
static const uint16_t SERVERSOCKET_LISTEN_PORT = 12345;
enum {BUFFER_SIZE = 100};

struct echo_client {
	struct cio_socket socket;
	uint8_t buffer[BUFFER_SIZE];
	struct cio_write_buffer wb;
	struct cio_write_buffer wbh;
	struct cio_read_buffer rb;
};

static struct cio_socket *alloc_echo_client(void)
{
	struct echo_client *client = malloc(sizeof(*client));
	if (cio_unlikely(client == NULL)) {
		return NULL;
	}

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


static void handle_write(struct cio_io_stream *stream, void *handler_context, const struct cio_write_buffer *buf, enum cio_error err, size_t bytes_transferred)
{
	(void)bytes_transferred;
	(void)buf;

	struct echo_client *client = handler_context;

	if (err != CIO_SUCCESS) {
		fprintf(stderr, "write error!\n");
		return;
	}

	cio_read_buffer_init(&client->rb, client->buffer, sizeof(client->buffer));
	stream->read_some(stream, &client->rb, handle_read, client);
}

static void handle_read(struct cio_io_stream *stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer)
{
	struct echo_client *client = handler_context;
	if (err == CIO_EOF) {
		fprintf(stdout, "connection close by peer\n");
		stream->close(stream);
		return;
	}

	if (err != CIO_SUCCESS) {
		fprintf(stderr, "read error!\n");
		return;
	}

	cio_write_buffer_head_init(&client->wbh);
	cio_write_buffer_element_init(&client->wb, cio_read_buffer_get_read_ptr(read_buffer), cio_read_buffer_unread_bytes(read_buffer));
	cio_write_buffer_queue_tail(&client->wbh, &client->wb);
	stream->write_some(stream, &client->wbh, handle_write, client);
}

static void handle_accept(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)handler_context;

	struct echo_client *client = cio_container_of(socket, struct echo_client, socket);

	if (err != CIO_SUCCESS) {
		fprintf(stderr, "accept error!\n");
		ss->close(ss);
		cio_eventloop_cancel(ss->impl.loop);
		return;
	}

	cio_read_buffer_init(&client->rb, client->buffer, sizeof(client->buffer));
	struct cio_io_stream *stream = socket->get_io_stream(socket);
	stream->read_some(stream, &client->rb, handle_read, client);
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

	struct cio_server_socket ss;
	err = cio_server_socket_init(&ss, &loop, SERVERSOCKET_BACKLOG, alloc_echo_client, free_echo_client, NULL);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	err = ss.set_reuse_address(&ss, true);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto close_socket;
	}

	err = ss.bind(&ss, NULL, SERVERSOCKET_LISTEN_PORT);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto close_socket;
	}

	err = ss.accept(&ss, handle_accept, NULL);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto close_socket;
	}

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
	}

close_socket:
	ss.close(&ss);
destroy_loop:
	cio_eventloop_destroy(&loop);
	return ret;
}
