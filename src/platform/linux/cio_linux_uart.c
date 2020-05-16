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
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_uart.h"

static const char DIR_NAME[] = "/dev/serial/by-path/";

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
			strncpy(src_buffer, DIR_NAME, sizeof(src_buffer));
			strncat(src_buffer, dir_entry->d_name, sizeof(src_buffer) - sizeof(DIR_NAME));

			char dst_buffer[PATH_MAX + 1];
			ssize_t name_len = readlink(src_buffer, dst_buffer, sizeof(dst_buffer));
			if (cio_unlikely(name_len == -1)) {
				return (enum cio_error)(-errno);
			}

			dst_buffer[name_len] = '\0';

			strncpy(src_buffer, DIR_NAME, sizeof(src_buffer));
			strncat(src_buffer, dir_entry->d_name, sizeof(src_buffer) - sizeof(DIR_NAME));

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
