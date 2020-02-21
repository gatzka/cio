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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cio_address_family.h"
#include "cio_compiler.h"
#include "cio_endian.h"
#include "cio_error_code.h"
#include "cio_inet_address.h"
#include "cio_socket_address.h"

enum cio_address_family cio_socket_address_get_family(const struct cio_socket_address *endpoint)
{
	return (enum cio_address_family)endpoint->impl.socket_address.addr.sa_family;
}

enum cio_error cio_init_inet_socket_address(struct cio_socket_address *sock_address, const struct cio_inet_address *inet_address, uint16_t port)
{
	if (cio_unlikely((sock_address == NULL) || (inet_address == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	enum cio_address_family family = inet_address->impl.family;
	if (cio_unlikely((family != CIO_ADDRESS_FAMILY_INET4) && (family != CIO_ADDRESS_FAMILY_INET6))) {
		return CIO_INVALID_ARGUMENT;
	}

	if (family == CIO_ADDRESS_FAMILY_INET4) {
		memset(&sock_address->impl.inet_addr4.impl.in, 0x0, sizeof(sock_address->impl.inet_addr4.impl.in));
		memcpy(&sock_address->impl.inet_addr4.impl.in.sin_addr, &inet_address->impl.in.s_addr, sizeof(inet_address->impl.in.s_addr));
		sock_address->impl.inet_addr4.impl.in.sin_port = cio_htobe16(port);
	} else {
		memset(&sock_address->impl.inet_addr6.impl.in6, 0x0, sizeof(sock_address->impl.inet_addr6.impl.in6));
		memcpy(sock_address->impl.inet_addr6.impl.in6.sin6_addr.s6_addr, inet_address->impl.in6.s6_addr, sizeof(inet_address->impl.in6.s6_addr));
		sock_address->impl.inet_addr6.impl.in6.sin6_port = cio_htobe16(port);
	}

	sock_address->impl.socket_address.addr.sa_family = (sa_family_t)family;

	return CIO_SUCCESS;
}
