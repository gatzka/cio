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

#ifndef CIO_SOCKET_H
#define CIO_SOCKET_H

#include <stdbool.h>
#include <stdint.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_export.h"
#include "cio_inet_socket_address.h"
#include "cio_io_stream.h"
#include "cio_socket_impl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the interface of a socket.
 *
 * You can @ref cio_socket_get_io_stream "get an I/O stream"
 * from a socket, @ref cio_socket_close "close" the socket or set
 * several socket options.
 */

struct cio_socket;

/**
 * @brief The type of a function that is called when
 * @ref cio_socket_connect "connect task" succeeds or fails.
 *
 * @param socket The socket where the connect was called on.
 * @param handler_context The context the functions works on.
 * @param err If err != ::CIO_SUCCESS, the connect call failed.
 */
typedef void (*cio_connect_handler)(struct cio_socket *socket, void *handler_context, enum cio_error err);

/**
 * @brief The type of close hook function.
 *
 * @param socket The cio_socket the close hook was called on.
 */
typedef void (*cio_socket_close_hook)(struct cio_socket *socket);

struct cio_socket {

	/**
	 * @privatesection
	 */
	struct cio_io_stream stream;
	cio_socket_close_hook close_hook;
	cio_connect_handler handler;
	void *handler_context;
	struct cio_socket_impl impl;
};

/**
 * @brief Initializes a cio_socket.
 *
 * @param socket The cio_socket that should be initialized.
 * @param address_family The address family like ::CIO_INET4_ADDRESS or ::CIO_INET6_ADDRESS
 * @param loop The event loop the socket shall operate on.
 * @param close_timeout_ns The timeout in ns until a closed TCP connection waits for a
 * TCP FIN packet from the remote peer before sending a TCP RST.If you set this parameter
 * to >0, you can effectivly control how long the TCP socket stays in the FIN_WAIT_2 state.
 * Setting this parameter to 0 leaves it up to the operating system to get the socket out of FIN_WAIT_2.
 * @param close_hook A close hook function. If this parameter is non @c NULL,
 * the function will be called directly after
 * @ref cio_socket_close "closing" the cio_socket.
 * It is guaranteed the the cio library will not access any memory of
 * cio_server_socket that is passed to the close hook. Therefore
 * the hook could be used to free the memory of the server socket.
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_socket_init(struct cio_socket *socket,
                                          enum cio_socket_address_family address_family,
                                          struct cio_eventloop *loop,
                                          uint64_t close_timeout_ns,
                                          cio_socket_close_hook close_hook);

/**
 * @brief Closes the cio_socket.
 *
 * Once a socket has been closed, no further communication is possible.
 * Closing the socket also closes the socket's cio_io_stream.
 *
 * @param socket A pointer to a cio_socket which shall be closed.
 */
CIO_EXPORT enum cio_error cio_socket_close(struct cio_socket *socket);

/**
 * @brief Connects this socket to a server.
 *
 * @param socket A pointer to a cio_socket which shall connect to a server.
 * @param endpoint The endpoint (IP address and port) of the server that should be connected.
 * @param handler The function to be called if the connect fails or succeeds.
 * @param handler_context The context passed to the @a handler function.
 */
CIO_EXPORT enum cio_error cio_socket_connect(struct cio_socket *socket, struct cio_inet_socket_address *endpoint, cio_connect_handler handler, void *handler_context);

/**
 * @brief Gets an I/O stream from the socket.
 *
 * @param socket A pointer to a cio_socket from which the cio_io_stream is retrieved.
 *
 * @return An I/O stream for reading from and writing to this socket.
 */
CIO_EXPORT struct cio_io_stream *cio_socket_get_io_stream(struct cio_socket *socket);

/**
 * @brief Enables/disables the Nagle algorithm
 *
 * @param socket A pointer to a cio_socket for which the Nagle algorithm should be changed.
 * @param on Whether Nagle algorithm should be enabled or not.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_socket_set_tcp_no_delay(struct cio_socket *socket, bool on);

/**
 * @brief Enables/disables TCP keepalive messages.
 *
 * @param socket A pointer to a cio_socket for which TCP keepalive should be changed.
 * @param on Whether or not to enable TCP keepalives.
 * @param keep_idle_s Time in seconds the connections needs to remain idle
 *        before start sending keepalive messages. This option might be unused
 *        in some platform implementations.
 * @param keep_intvl_s Time in seconds between individual keepalive probes.
 *        This option might be unused in some platform implementations.
 * @param keep_cnt The maximum number of keepalive probes before dropping the connection.
 *        This option might be unused in some platform implementations.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_socket_set_keep_alive(struct cio_socket *socket, bool on, unsigned int keep_idle_s, unsigned int keep_intvl_s, unsigned int keep_cnt);

#ifdef __cplusplus
}
#endif

#endif
