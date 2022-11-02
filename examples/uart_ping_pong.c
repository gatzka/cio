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

#include "cio/buffered_stream.h"
#include "cio/compiler.h"
#include "cio/error_code.h"
#include "cio/eventloop.h"
#include "cio/read_buffer.h"
#include "cio/uart.h"
#include "cio/write_buffer.h"

enum { BUFFER_SIZE = 128 };

struct client {
	size_t bytes_read;
	struct cio_buffered_stream bs;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
	struct cio_read_buffer rb;
	uint8_t buffer[BUFFER_SIZE];
};

static struct client client1;
static struct client client2;

static const char HELLO[] = "Hello";

static struct cio_eventloop loop;

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void client_handle_read(struct cio_buffered_stream *buffered_stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t num_bytes);

static void client_handle_write(struct cio_buffered_stream *buffered_stream, void *handler_context, enum cio_error err)
{
	struct client *client = handler_context;

	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "client failed to write!\n");
		return;
	}

	(void)fprintf(stdout, "Client wrote data, now receiving...\n");

	cio_read_buffer_consume(&client->rb, client->bytes_read);

	err = cio_buffered_stream_read_at_least(buffered_stream, &client->rb, sizeof(HELLO), client_handle_read, client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "server could no start reading!\n");
		return;
	}
}

static void client_handle_read(struct cio_buffered_stream *buffered_stream, void *handler_context, enum cio_error err, struct cio_read_buffer *read_buffer, size_t num_bytes)
{
	struct client *client = handler_context;

	if (cio_unlikely(err == CIO_EOF)) {
		(void)fprintf(stdout, "connection closed by peer\n");
		cio_buffered_stream_close(buffered_stream);
		return;
	}

	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "read error!\n");
		cio_buffered_stream_close(buffered_stream);
		return;
	}

	uint8_t recv_buffer[BUFFER_SIZE];
	memcpy(recv_buffer, cio_read_buffer_get_read_ptr(read_buffer), num_bytes);
	recv_buffer[num_bytes] = '\0';
	(void)fprintf(stdout, "Client received data: %s , now sending...\n", recv_buffer);

	client->bytes_read = num_bytes;
	cio_write_buffer_head_init(&client->wbh);
	cio_write_buffer_element_init(&client->wb, cio_read_buffer_get_read_ptr(read_buffer), num_bytes);
	cio_write_buffer_queue_tail(&client->wbh, &client->wb);
	cio_buffered_stream_write(buffered_stream, &client->wbh, client_handle_write, client);
}

static struct cio_io_stream *setup_uart(struct cio_uart *uart)
{
	enum cio_error err = cio_uart_set_num_data_bits(uart, CIO_UART_8_DATA_BITS);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "Could not set 8 data bits per word on UART!\n");
		goto err;
	}
	err = cio_uart_set_parity(uart, CIO_UART_PARITY_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "Could not set parity on UART!\n");
		goto err;
	}
	err = cio_uart_set_num_stop_bits(uart, CIO_UART_ONE_STOP_BIT);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "Could not set 1 stop bit on UART!\n");
		goto err;
	}
	err = cio_uart_set_baud_rate(uart, CIO_UART_BAUD_RATE_115200);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "Could not set baud rate on UART!\n");
		goto err;
	}
	err = cio_uart_set_flow_control(uart, CIO_UART_FLOW_CONTROL_NONE);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto err;
	}
	struct cio_io_stream *stream = cio_uart_get_io_stream(uart);
	if (cio_unlikely(stream == NULL)) {
		(void)fprintf(stderr, "failed to get IO stream!\n");
		goto err;
	}
	err = cio_buffered_stream_init(&client1.bs, stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "failed to init buffered stream!\n");
		goto err;
	}
	return stream;

err:
	return NULL;
}

static struct cio_uart *detect_uart(const char *uart_name)
{
	size_t num_uarts = cio_uart_get_number_of_uarts();
	(void)fprintf(stdout, "found %zu uart(s)\n", num_uarts);

	if (num_uarts < 2) {
		(void)fprintf(stderr, "not enough uarts to play ping pong\n");
		return NULL;
	}

	struct cio_uart *uarts = malloc(sizeof(*uarts) * num_uarts);
	if (cio_unlikely(uarts == NULL)) {
		return NULL;
	}

	size_t detected_ports = 0;
	enum cio_error err = cio_uart_get_ports(uarts, num_uarts, &detected_ports);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "Could not get UART information!\n");
		goto err;
	}

	for (size_t i = 0; i < detected_ports; i++) {
		if (strcmp(cio_uart_get_name(&uarts[i]), uart_name) == 0) {
			struct cio_uart *uart = malloc(sizeof(*uarts) * num_uarts);
			if (cio_unlikely(uart == NULL)) {
				(void)fprintf(stderr, "Could not allocate UART structure!\n");
				goto err;
			}
			*uart = uarts[i];
			return uart;
		}
	}

err:
	free(uarts);
	return NULL;
}

static enum cio_error setup_client(struct client *client, struct cio_io_stream *stream)
{
	enum cio_error err = cio_buffered_stream_init(&client->bs, stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "failed to init buffered stream!\n");
		return EXIT_FAILURE;
	}
	err = cio_read_buffer_init(&client->rb, client->buffer, sizeof(client->buffer));
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "failed to init read buffer!\n");
		return EXIT_FAILURE;
	}

	return CIO_SUCCESS;
}

int main(void)
{
	int ret = EXIT_SUCCESS;
	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		return EXIT_FAILURE;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		(void)signal(SIGTERM, SIG_DFL);
		return EXIT_FAILURE;
	}

	enum cio_error err = cio_eventloop_init(&loop);
	if (err != CIO_SUCCESS) {
		return EXIT_FAILURE;
	}

	size_t num_uarts = cio_uart_get_number_of_uarts();
	(void)fprintf(stdout, "found %zu uart(s)\n", num_uarts);

	if (num_uarts < 2) {
		(void)fprintf(stderr, "not enough uarts to play ping pong\n");
		ret = EXIT_FAILURE;
		goto destroy_ev;
	}

	struct cio_uart *uart1 = detect_uart("/dev/ttyUSB0");

	err = cio_uart_init(uart1, &loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "Could not get init first UART!\n");
		ret = EXIT_FAILURE;
		goto free_first_uart;
	}

	struct cio_io_stream *stream = setup_uart(uart1);
	if (cio_unlikely(stream == NULL)) {
		(void)fprintf(stderr, "failed to get IO stream!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = setup_client(&client1, stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "failed to setup client!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}
	err = cio_buffered_stream_read_at_least(&client1.bs, &client1.rb, sizeof(HELLO), client_handle_read, &client1);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "server could no start reading!\n");
		ret = EXIT_FAILURE;
		goto close_first_uart;
	}

	struct cio_uart *uart2 = detect_uart("/dev/ttyUSB1");
	err = cio_uart_init(uart2, &loop, NULL);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "Could not get init second UART!\n");
		ret = EXIT_FAILURE;
		goto free_second_uart;
	}
	stream = setup_uart(uart2);
	if (cio_unlikely(stream == NULL)) {
		(void)fprintf(stderr, "failed to get IO stream!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	err = setup_client(&client1, stream);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "failed to setup client!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}
	cio_write_buffer_head_init(&client2.wbh);
	cio_write_buffer_const_element_init(&client2.wb, HELLO, sizeof(HELLO));
	cio_write_buffer_queue_tail(&client2.wbh, &client2.wb);
	err = cio_buffered_stream_write(&client2.bs, &client2.wbh, client_handle_write, &client2);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		(void)fprintf(stderr, "Could not send data on UART!\n");
		ret = EXIT_FAILURE;
		goto close_uarts;
	}

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
	}
close_uarts:
	cio_uart_close(uart2);
free_second_uart:
	free(uart2);
close_first_uart:
	cio_uart_close(uart1);
free_first_uart:
	free(uart1);
destroy_ev:
	cio_eventloop_destroy(&loop);
	return ret;
}
