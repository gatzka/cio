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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_uart.h"

static struct cio_eventloop loop;

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

int main(void)
{
	int ret = EXIT_SUCCESS;
	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		return EXIT_FAILURE;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		signal(SIGTERM, SIG_DFL);
		return EXIT_FAILURE;
	}

	enum cio_error err = cio_eventloop_init(&loop);
	if (err != CIO_SUCCESS) {
		return EXIT_FAILURE;
	}

	size_t num_uarts = cio_uart_get_number_of_uarts();
	fprintf(stdout, "found %zu uart(s)\n", num_uarts);
	
	if (num_uarts < 2) {
		fprintf(stderr, "not enough uarts to play ping pong\n");
		return EXIT_SUCCESS;
	}

	struct cio_uart *uarts = malloc(sizeof(*uarts) * num_uarts);
	if (cio_unlikely(uarts == NULL)) {
		ret = EXIT_FAILURE;
		goto destroy_ev;
	}

	size_t detected_ports = 0;
	err = cio_uart_get_ports(uarts, num_uarts, &detected_ports);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not get UART information!\n");
		ret = EXIT_FAILURE;
		goto free_uarts;
	}

	for (size_t i = 0; i < detected_ports; i++) {
		fprintf(stdout, "detected port %zu: %s\n", i, uarts[i].impl.name);
	}

	err = cio_uart_init(&uarts[0], &loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not get init first UART!\n");
		ret = EXIT_FAILURE;
		goto free_uarts;
	}
	
	err = cio_uart_set_parity(&uarts[0], CIO_UART_PARITY_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set parity on first UART!\n");
		ret = EXIT_FAILURE;
		goto free_uarts;
	}




	err = cio_uart_init(&uarts[1], &loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not get init second UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}

	err = cio_uart_set_parity(&uarts[1], CIO_UART_PARITY_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set parity on second UART!\n");
		ret = EXIT_FAILURE;
		goto free_uarts;
	}

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
	}

	cio_uart_close(&uarts[1]);
close_first_uart:
	cio_uart_close(&uarts[0]);
free_uarts:
	free(uarts);
destroy_ev:
	cio_eventloop_destroy(&loop);
	return ret;
}
