/*
 * SPDX-License-Identifier: MIT
 *
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

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cio/compiler.h"
#include "cio/error_code.h"
#include "cio/inet_address.h"
#include "cio/linux_socket_utils.h"

int cio_linux_socket_create(enum cio_address_family address_family)
{
	if (cio_unlikely(address_family == CIO_ADDRESS_FAMILY_UNSPEC)) {
		errno = EINVAL;
		return -1;
	}

	int domain = (int)address_family;

	int fd = socket(domain, (unsigned int)SOCK_STREAM | (unsigned int)SOCK_CLOEXEC | (unsigned int)SOCK_NONBLOCK, 0U);
	if (cio_unlikely(fd == -1)) {
		return -1;
	}

	if (address_family == CIO_ADDRESS_FAMILY_INET6) {
		int yes = 1;
		int ret = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&yes, sizeof(yes));
		if (cio_unlikely(ret == -1)) {
			close(fd);
			return -1;
		}
	}

	return fd;
}

enum cio_error cio_linux_get_socket_error(int fd)
{
	int error = 0;
	socklen_t len = sizeof(error);
	int ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
	if (cio_unlikely(ret != 0)) {
		return (enum cio_error)(-errno);
	}

	return (enum cio_error)(-error);
}
