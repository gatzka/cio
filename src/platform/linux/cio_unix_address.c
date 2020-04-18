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

#include <stdbool.h>
#include <string.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_export.h"
#include "cio_socket_address.h"
#include "platform/linux/cio_unix_address.h"

static bool is_abstract_uds(const char *path)
{
	return path[0] == '\0';
}

CIO_EXPORT enum cio_error cio_init_uds_socket_address(struct cio_socket_address *sock_address, const char *path)
{
	if (cio_unlikely((sock_address == NULL) || (path == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	size_t max_path_length = sizeof(sock_address->impl.sa.unix_address.un.sun_path);
	size_t path_length = 0;
	if (is_abstract_uds(path)) {
		path_length = strlen(path + 1) + 1;
	} else {
		path_length = strlen(path) + 1;
	}

	if (cio_unlikely(path_length > max_path_length)) {
		return CIO_INVALID_ARGUMENT;
	}

	memset(&sock_address->impl.sa.unix_address.un, 0x0, sizeof(sock_address->impl.sa.unix_address.un));
	sock_address->impl.sa.socket_address.addr.sa_family = (sa_family_t)CIO_ADDRESS_FAMILY_UNIX;
	memcpy(sock_address->impl.sa.unix_address.un.sun_path, path, path_length);
	sock_address->impl.len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_length);

	return CIO_SUCCESS;
}
