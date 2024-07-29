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

#ifndef CIO_LINUX_SOCKET_ADDRESS_IMPL_H
#define CIO_LINUX_SOCKET_ADDRESS_IMPL_H

#include <sys/socket.h>

#include "cio/error_code.h"
#include "cio/export.h"
#include "cio/inet4_socket_address.h"
#include "cio/inet6_socket_address.h"
#include "cio/inet_address.h"
#include "cio/unix_address.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_inet_address;
struct cio_socket_address;

struct cio_socket_address_common {
	struct sockaddr addr;
};

union cio_sa {
	struct cio_socket_address_common socket_address;
	struct cio_inet4_socket_address inet_addr4;
	struct cio_inet6_socket_address inet_addr6;
	struct cio_unix_address unix_address;
};

struct cio_socket_address_impl {
	socklen_t len;
	union cio_sa sa;
};

#ifdef __cplusplus
}
#endif

#endif
