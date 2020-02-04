/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2020> <Stephan Gatzka>
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
#include "cio_inet_address.h"
#include "cio_socket.h"

static struct cio_eventloop loop;
static const uint64_t close_timeout_ns = UINT64_C(1) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);

static void handle_connect(struct cio_socket *socket, void *handler_context, enum cio_error err)
{
	(void)handler_context;

	if (err != CIO_SUCCESS) {
		fprintf(stderr, "connect error, error code %d\n", err);
	}

	cio_socket_close(socket);
	cio_eventloop_cancel(socket->impl.loop);
}

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s <IPv4 address> <port>\n", name);
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	uint8_t ipv4_address[4] = {0x8b, 0x3b, 0x86, 0xf8};
	int number = sscanf(argv[1], "%3hhu.%3hhu.%3hhu.%3hhu", ipv4_address, ipv4_address + 1, ipv4_address + 2, ipv4_address + 3);
	if (number != 4) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	uint16_t port;
	number = sscanf(argv[2], "%hu", &port);

	if (number != 1) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

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

	struct cio_inet_address address;
	err = cio_init_inet_address(&address, ipv4_address, sizeof(ipv4_address));
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	struct cio_inet_socket_address socket_address;
	err = cio_init_inet_socket_address(&socket_address, &address, port);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	struct cio_socket socket;

	err = cio_socket_init(&socket, CIO_INET4_ADDRESS, &loop, close_timeout_ns, NULL);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	err = cio_socket_connect(&socket, &socket_address, handle_connect, NULL);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto close_socket;
	}

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
	}

close_socket:
	cio_socket_close(&socket);
destroy_loop:
	cio_eventloop_destroy(&loop);
	return ret;
}
