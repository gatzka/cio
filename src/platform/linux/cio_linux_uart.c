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

#define _DEFAULT_SOURCE
#include <dirent.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "cio_uart.h"

size_t cio_uart_get_number_of_uarts(void)
{
	const char dir_name[] = "/dev/serial/by-path/";
	DIR *serial_dir = opendir(dir_name);
	if (serial_dir == NULL) {
		return 0;
	}

	struct dirent *dir_entry = readdir(serial_dir);
	size_t num_uarts = 0;
	while (dir_entry) {
		if (dir_entry->d_type == DT_LNK) {
			//char src_buffer[PATH_MAX];
			//strncpy(src_buffer, dir_name, sizeof(src_buffer));
			//strcat(src_buffer, dir_entry->d_name);

			//char dst_buffer[PATH_MAX];
			//ssize_t name_len = readlink(src_buffer, dst_buffer, sizeof(dst_buffer));
			//strncpy(src_buffer, dir_name, sizeof(src_buffer));
			//strcat(src_buffer, dst_buffer);
			//char *rp = realpath(src_buffer, NULL);
			num_uarts++;
		}

		dir_entry = readdir(serial_dir);
	}

	closedir(serial_dir);
	return num_uarts;
}
