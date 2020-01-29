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

#ifndef CIO_INET_ADDRESS_H
#define CIO_INET_ADDRESS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Representation of an Internet Protocol (IP) address.
 */

#include <stddef.h>
#include <stdint.h>

#include "cio_error_code.h"
#include "cio_export.h"
#include "cio_inet4_address.h"
#include "cio_inet6_address.h"

enum cio_inet_address_type {
	CIO_INET4_ADDRESS,
	CIO_INET6_ADDRESS
};

struct cio_inet_address {
	enum cio_inet_address_type type;
	union {
		struct cio_inet4_address addr4;
		struct cio_inet6_address addr6;
	} address;
};

/**
 * @brief Initializes a inet address structure.
 *
 * @param inet_address The inet address to be initialized.
 * @param address The buffer that holds the address in network byte order.
 * @param address_length The length of the address buffer. Must be either 4 for IPv4 addresses or 16 for IPv6 addresses.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_init_inet_address(struct cio_inet_address *inet_address, const uint8_t *address, size_t address_length);

#ifdef __cplusplus
}
#endif

#endif
