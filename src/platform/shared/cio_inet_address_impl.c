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
#include "cio_error_code.h"
#include "cio_inet_address.h"
#include "cio_inet_address_impl.h"

static const struct cio_inet_address inet_address_any6 = {
    .impl = {.family = CIO_ADDRESS_FAMILY_INET6, .in6 = IN6ADDR_ANY_INIT}};

static const struct cio_inet_address inet_address_any4 = {
    .impl = {.family = CIO_ADDRESS_FAMILY_INET4, .in.s_addr = INADDR_ANY}};

const struct cio_inet_address *cio_get_inet_address_any4(void)
{
	return &inet_address_any4;
}

const struct cio_inet_address *cio_get_inet_address_any6(void)
{
	return &inet_address_any6;
}

enum cio_error cio_init_inet_address(struct cio_inet_address *inet_address, const uint8_t *address, size_t address_length)
{
	size_t v4_size = sizeof(inet_address->impl.in.s_addr);
	size_t v6_size = sizeof(inet_address->impl.in6.s6_addr);
	if (cio_unlikely((inet_address == NULL) || (address == NULL) || ((address_length != v4_size) && (address_length != v6_size)))) {
		return CIO_INVALID_ARGUMENT;
	}

	if (address_length == v4_size) {
		inet_address->impl.family = CIO_ADDRESS_FAMILY_INET4;
		memcpy(&inet_address->impl.in.s_addr, address, address_length);
	} else {
		inet_address->impl.family = CIO_ADDRESS_FAMILY_INET6;
		memcpy(&inet_address->impl.in6.s6_addr, address, address_length);
	}

	return CIO_SUCCESS;
}

enum cio_address_family cio_inet_address_get_family(const struct cio_inet_address *endpoint)
{
	return endpoint->impl.family;
}
