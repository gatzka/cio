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

#include "cio/cio_buffered_stream.h"
#include "cio/cio_compiler.h"
#include "cio/cio_error_code.h"
#include "cio/cio_eventloop.h"
#include "cio/cio_read_buffer.h"
#include "cio/cio_uart.h"
#include "cio/cio_write_buffer.h"

enum { BUFFER_SIZE = 128 };

struct client {
	size_t bytes_read;
	struct cio_buffered_stream bs;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
	struct cio_read_buffer rb;
	uint8_t buffer[BUFFER_SIZE];
};

static struct client client;
static struct client client2;

static const char HELLO[] = "Hello";

static struct cio_eventloop loop;

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void client_handle_read(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t num_bytes);

static void client_handle_write(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	struct client *c = handler_context;

	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "client failed to write!\n");
		return;
	}

	fprintf(stdout, "Client wrote data, now receiving...\n");

	cio_read_buffer_consume(&c->rb, c->bytes_read);

	err = cio_buffered_stream_read_at_least(bs, &c->rb, sizeof(HELLO), client_handle_read, c);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "server could no start reading!\n");
		return;
	}
}

static void client_handle_read(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t num_bytes)
{
	struct client *c = handler_context;

	if (cio_unlikely(err == CIO_EOF)) {
		fprintf(stdout, "connection closed by peer\n");
		cio_buffered_stream_close(bs);
		return;
	}

	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "read error!\n");
		cio_buffered_stream_close(bs);
		return;
	}

	uint8_t recv_buffer[BUFFER_SIZE];
	memcpy(recv_buffer, cio_read_buffer_get_read_ptr(read_buffer), num_bytes);
	recv_buffer[num_bytes] = '\0';
	fprintf(stdout, "Client received data: %s , now sending...\n", recv_buffer);

	c->bytes_read = num_bytes;
	cio_write_buffer_head_init(&c->wbh);
	cio_write_buffer_element_init(&c->wb, cio_read_buffer_get_read_ptr(read_buffer), num_bytes);
	cio_write_buffer_queue_tail(&c->wbh, &c->wb);
	cio_buffered_stream_write(bs, &c->wbh, client_handle_write, c);
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
		fprintf(stdout, "detected port %zu: %s\n", i, cio_uart_get_name(&uarts[i]));
	}

	struct cio_uart *uart1 = NULL;
	struct cio_uart *uart2 = NULL;
	for (size_t i = 0; i < detected_ports; i++) {
		if (strcmp(cio_uart_get_name(&uarts[i]), "/dev/ttyUSB0") == 0) {
			uart1 = &uarts[i];
		} else if (strcmp(cio_uart_get_name(&uarts[i]), "/dev/ttyUSB1") == 0) {
			uart2 = &uarts[i];
		}
	}

	err = cio_uart_init(uart1, &loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not get init first UART!\n");
		ret = EXIT_FAILURE;
		goto free_uarts;
	}
	err = cio_uart_set_num_data_bits(uart1, CIO_UART_8_DATA_BITS);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set 8 data bits per word on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_parity(uart1, CIO_UART_PARITY_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set parity on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_num_stop_bits(uart1, CIO_UART_ONE_STOP_BIT);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set 1 stop bit on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_baud_rate(uart1, CIO_UART_BAUD_RATE_115200);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set baud rate on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_flow_control(uart1, CIO_UART_FLOW_CONTROL_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not disable flow control on first UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	struct cio_io_stream *stream = cio_uart_get_io_stream(uart1);
	if (cio_unlikely(stream == NULL)) {
		fprintf(stderr, "failed to get IO stream!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_buffered_stream_init(&client.bs, stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "failed to init buffered stream!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_read_buffer_init(&client.rb, client.buffer, sizeof(client.buffer));
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "failed to init read buffer!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_buffered_stream_read_at_least(&client.bs, &client.rb, sizeof(HELLO), client_handle_read, &client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "server could no start reading!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}

	err = cio_uart_init(uart2, &loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not get init second UART!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_uart_set_num_data_bits(uart2, CIO_UART_8_DATA_BITS);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set 8 data bits per word on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_uart_set_parity(uart2, CIO_UART_PARITY_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set parity on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_uart_set_num_stop_bits(uart2, CIO_UART_ONE_STOP_BIT);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set 1 stop bit on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_uart_set_baud_rate(uart2, CIO_UART_BAUD_RATE_115200);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not set baud rate on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_uart_set_flow_control(uart2, CIO_UART_FLOW_CONTROL_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not disable flow control on second UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	stream = cio_uart_get_io_stream(uart2);
	if (cio_unlikely(stream == NULL)) {
		fprintf(stderr, "failed to get IO stream!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_buffered_stream_init(&client2.bs, stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "failed to init buffered stream!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = cio_read_buffer_init(&client2.rb, client2.buffer, sizeof(client2.buffer));
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "failed to init read buffer!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	cio_write_buffer_head_init(&client2.wbh);
	cio_write_buffer_const_element_init(&client2.wb, HELLO, sizeof(HELLO));
	cio_write_buffer_queue_tail(&client2.wbh, &client2.wb);
	err = cio_buffered_stream_write(&client2.bs, &client2.wbh, client_handle_write, &client2);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		fprintf(stderr, "Could not send data on UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
	}
close_uarts:
	cio_uart_close(uart2);
close_first_uart:
	cio_uart_close(uart1);
free_uarts:
	free(uarts);
destroy_ev:
	cio_eventloop_destroy(&loop);
	return ret;
}
