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
#include <string.h>

#include "cio_buffered_stream.h"
#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_uart.h"
#include "cio_write_buffer.h"

static const char HELLO[] = "Hello";

static struct cio_eventloop loop;

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void client_handle_write(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	(void)bs;
	(void)handler_context;

	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Error while transmitting over uart!\n");
		return;
	}

	fprintf(stdout, "written!\n");
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

#if 0
	if (num_uarts < 2) {
		fprintf(stderr, "not enough uarts to play ping pong\n");
		return EXIT_SUCCESS;
	}
#endif

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

	struct cio_uart *uart = NULL;
	for (size_t i = 0; i < detected_ports; i++) {
		if (strcmp(uarts[i].impl.name, "/dev/ttyUSB0") == 0) {
			uart = &uarts[i];
			break;
		}
	}

	err = cio_uart_init(uart, &loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not get init first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_num_data_bits(uart, CIO_UART_8_DATA_BITS);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set 8 data bits per word on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_parity(uart, CIO_UART_PARITY_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set parity on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_num_stop_bits(uart, CIO_UART_ONE_STOP_BIT);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set 1 stop bit on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_baud_rate(uart, CIO_UART_BAUD_RATE_115200);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set baud rate on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_flow_control(uart, CIO_UART_FLOW_CONTROL_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not disable flow control on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}

	struct cio_io_stream *stream = cio_uart_get_io_stream(uart);
	struct cio_buffered_stream bs;
	cio_buffered_stream_init(&bs, stream);
	struct cio_write_buffer wbh;
	cio_write_buffer_head_init(&wbh);
	struct cio_write_buffer wb;
	cio_write_buffer_const_element_init(&wb, HELLO, sizeof(HELLO));
	cio_write_buffer_queue_tail(&wbh, &wb);

	err = cio_buffered_stream_write(&bs, &wbh, client_handle_write, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not send data on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
#if 0
	err = cio_uart_init(&uarts[1], &loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not get init second UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}

	err = cio_uart_set_num_data_bits(&uarts[1], CIO_UART_8_DATA_BITS);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set 8 data bits per word on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_uart_set_parity(&uarts[1], CIO_UART_PARITY_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set parity on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_uart_set_num_stop_bits(&uarts[1], CIO_UART_ONE_STOP_BIT);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set 1 stop bit on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_uart_set_baud_rate(&uarts[1], CIO_UART_BAUD_RATE_115200);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set baud rate on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_uart_set_flow_control(&uarts[1], CIO_UART_FLOW_CONTROL_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not disable flow control on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
#endif

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
	}
#if 0
close_uarts:
	cio_uart_close(&uarts[1]);
close_first_uart:
	cio_uart_close(&uarts[0]);
#endif

close_first_uart:
	cio_uart_close(uart);
free_uarts:
	free(uarts);
destroy_ev:
	cio_eventloop_destroy(&loop);
	return ret;
}
