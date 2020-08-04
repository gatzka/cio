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

#ifndef CIO_SOCKET_ADDRESS_H
#define CIO_SOCKET_ADDRESS_H

#include <stdint.h>

#include "cio/cio_address_family.h"
#include "cio/cio_error_code.h"
#include "cio/cio_export.h"
#include "cio/cio_inet_address.h"
#include "cio/cio_socket_address_impl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_socket_address {
	struct cio_socket_address_impl impl;
};

/**
 * @brief Initializes a inet socket address from an IP address and a port number.
 *
 * @param sock_address The inet socket address to be initalized.
 * @param inet_address The IP address.
 * @param port The port number.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_init_inet_socket_address(struct cio_socket_address *sock_address, const struct cio_inet_address *inet_address, uint16_t port);

/**
 * @brief Initializes a inet socket address from an IP address and a port number.
 *
 * @param sock_address The inet socket address to be initalized.
 * @param path The path to the unix domain socket file.
 * @note Linux systems have the special ability to create an "abstract" unix domain socket.
 * Abstract domain sockets do note reside as a file in a filesystem so they have the advantage
 * that they disappear when all open references to that socket are closed.
 * @note Abstract sockets are distinguished by the fact that path[0] is a null byte ('\0').
 * @warning Please note if you are using non-abstract domain sockets, it is the responsibility
 * of the user of this library to remove the unix domain socket file when the program ends.
 * A natural place to do this would be the close_hook provided in ::cio_server_socket_init.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_init_uds_socket_address(struct cio_socket_address *sock_address, const char *path);

/**
 * @brief Get the address family of an initialized socket address.
 *
 * @param endpoint The socket address endpoint from which the address family should be retrieved.
 *
 * @return The address family.
 */
CIO_EXPORT enum cio_address_family cio_socket_address_get_family(const struct cio_socket_address *endpoint);

#ifdef __cplusplus
}
#endif

#endif
