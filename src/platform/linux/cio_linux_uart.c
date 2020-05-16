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

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_uart.h"
#include "cio_util.h"

static const char DIR_NAME[] = "/dev/serial/by-path/";

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_SUCCESS;
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_SUCCESS;
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_uart *port = cio_container_of(stream, struct cio_uart, stream);
	return cio_uart_close(port);
}

size_t cio_uart_get_number_of_uarts(void)
{
	DIR *serial_dir = opendir(DIR_NAME);
	if (serial_dir == NULL) {
		return 0;
	}

	struct dirent *dir_entry = readdir(serial_dir);
	size_t num_uarts = 0;
	while (dir_entry) {
		if (dir_entry->d_type == DT_LNK) {
			num_uarts++;
		}

		dir_entry = readdir(serial_dir);
	}

	closedir(serial_dir);
	return num_uarts;
}

enum cio_error cio_uart_get_ports(struct cio_uart ports[], size_t num_ports_entries, size_t *num_detected_ports)
{
	DIR *serial_dir = opendir(DIR_NAME);
	if (serial_dir == NULL) {
		return 0;
	}

	struct dirent *dir_entry = readdir(serial_dir);
	size_t num_uarts = 0;
	while (dir_entry) {
		if (num_uarts >= num_ports_entries) {
			*num_detected_ports = num_uarts;
			return CIO_SUCCESS;
		}

		if (dir_entry->d_type == DT_LNK) {
			char src_buffer[PATH_MAX + 1];
			strncpy(src_buffer, DIR_NAME, sizeof(src_buffer) - 1);
			size_t len = strlen(src_buffer);
			strncpy(src_buffer + len, dir_entry->d_name, sizeof(src_buffer) - len);

			char dst_buffer[PATH_MAX + 1];
			ssize_t name_len = readlink(src_buffer, dst_buffer, sizeof(dst_buffer));
			if (cio_unlikely(name_len == -1)) {
				return (enum cio_error)(-errno);
			}

			dst_buffer[name_len] = '\0';

			strncpy(src_buffer, DIR_NAME, sizeof(src_buffer) - 1);
			len = strlen(src_buffer);
			strncpy(src_buffer + len, dir_entry->d_name, sizeof(src_buffer) - len);

			char *rp = realpath(src_buffer, ports[num_uarts].impl.name);
			if (cio_unlikely(rp == NULL)) {
				return (enum cio_error)(-errno);
			}

			num_uarts++;
		}

		dir_entry = readdir(serial_dir);
	}

	closedir(serial_dir);
	*num_detected_ports = num_uarts;
	return CIO_SUCCESS;
}

enum cio_error cio_uart_init(struct cio_uart *port, struct cio_eventloop *loop, cio_uart_close_hook_t close_hook)
{
	if (cio_unlikely((port == NULL) || (loop == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	port->impl.ev.fd = open(port->impl.name, (unsigned int)O_RDWR | (unsigned int)O_CLOEXEC | (unsigned int)O_NONBLOCK);
	if (cio_unlikely(port->impl.ev.fd < 0)) {
		return (enum cio_error)(-errno);
	}

	port->close_hook = close_hook;

	port->stream.read_some = stream_read;
	port->stream.write_some = stream_write;
	port->stream.close = stream_close;

	port->impl.loop = loop;

	enum cio_error err = cio_linux_eventloop_add(port->impl.loop, &port->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto ev_add_failed;
	}

	err = cio_linux_eventloop_register_read(port->impl.loop, &port->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto ev_reg_read_failed;
	}

	return CIO_SUCCESS;

ev_reg_read_failed:
	cio_linux_eventloop_remove(port->impl.loop, &port->impl.ev);
ev_add_failed:
	close(port->impl.ev.fd);
	return err;
}

enum cio_error cio_uart_close(struct cio_uart *port)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	close(port->impl.ev.fd);
	if (port->close_hook != NULL) {
		port->close_hook(port);
	}

	cio_linux_eventloop_unregister_read(port->impl.loop, &port->impl.ev);
	cio_linux_eventloop_remove(port->impl.loop, &port->impl.ev);

	return CIO_SUCCESS;
}
