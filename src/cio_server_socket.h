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

#ifndef CIO_SERVER_SOCKET_H
#define CIO_SERVER_SOCKET_H

#include <stdbool.h>
#include <stdint.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_export.h"
#include "cio_inet_socket_address.h"
#include "cio_server_socket_impl.h"
#include "cio_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the interface of a server socket.
 *
 * A server socket can be @ref cio_server_socket_init "initialized",
 * @ref cio_server_socket_accept "accept connections" and can be
 * @ref cio_server_socket_close "closed".
 */

typedef struct cio_socket *(*cio_alloc_client)(void);
typedef void (*cio_free_client)(struct cio_socket *socket);

struct cio_server_socket;

/**
 * @brief The type of a function that is called when
 * @ref cio_server_socket_accept "accept task" succeeds or fails.
 *
 * @param ss The server socket where the accept was called on.
 * @param handler_context The context the functions works on.
 * @param err If err != ::CIO_SUCCESS, the accept call failed. Please note that no memory for a client is
 * allocated if err != ::CIO_SUCCESS. So NEVER close @a socket in that case.
 * @param socket The client socket that was created from the accept.
 */
typedef void (*cio_accept_handler)(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket);

/**
 * @brief The type of close hook function.
 *
 * @param ss The cio_linux_server_socket the close hook was called on.
 */
typedef void (*cio_server_socket_close_hook)(struct cio_server_socket *ss);

/**
 * @brief The cio_server_socket struct describes a server socket.
 */
struct cio_server_socket {
	/**
	 * @privatesection
	 */
	struct cio_server_socket_impl impl;
	int backlog;
	cio_server_socket_close_hook close_hook;
	cio_accept_handler handler;
	void *handler_context;
	cio_alloc_client alloc_client;
	cio_free_client free_client;
};

/**
 * @brief Initializes a cio_server_socket.
 *
 * @param ss The cio_server_socket that should be initialized.
 * @param loop The event loop the server socket shall operate on.
 * @param backlog The minimal length of the listen queue.
 * @param alloc_client An allocator function that is called if a client connects
 * to the server.
 * @param free_client This function is called after closing a @ref cio_socket.
 * It can and should be used to free resources allocated with @p alloc_client.
 * @param close_timeout_ns The timeout in ns until a closed TCP connection waits for a
 * TCP FIN packet from teh remote peer before sending a TCP RST.If you set this parameter
 * to >0, you can effectivly control how long the TCP socket stays in the FIN_WAIT_2 state.
 * Setting this parameter to 0 leaves it up to the operating system to get the socket out of FIN_WAIT_2.
 * @param close_hook A close hook function. If this parameter is non @c NULL,
 * the function will be called directly after
 * @ref cio_server_socket_close "closing" the cio_server_socket.
 * It is guaranteed the the cio library will not access any memory of
 * cio_server_socket that is passed to the close hook. Therefore
 * the hook could be used to free the memory of the server socket.
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                                 struct cio_eventloop *loop,
                                                 unsigned int backlog,
                                                 cio_alloc_client alloc_client,
                                                 cio_free_client free_client,
                                                 uint64_t close_timeout_ns,
                                                 cio_server_socket_close_hook close_hook);

/**
 * @brief Accepts an incoming socket connection.
 *
 * If the platform specific accept implementation fails, @p handler will
 * be called with a appropriate error code. The implementation specific accept
 * needs to take care of freeing any intermediatly allocated memory, so there is no
 * need to free any resources in @p handler if @p handler is called with err != ::CIO_SUCCESS.
 *
 * @param ss A pointer to a cio_server_socket on which the accept should be performed.
 * @param handler The function to be called if the accept fails or succeeds.
 * @param handler_context The context passed to the @a handler function.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_server_socket_accept(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context);

/**
 * @brief Closes the server socket.
 *
 * @param ss A pointer to a cio_server_socket on which the close should be performed.
 */
CIO_EXPORT void cio_server_socket_close(struct cio_server_socket *ss);

/**
 * @brief Binds the cio_server_socket to a specific address
 *
 * @param ss A pointer to a cio_server_socket on which the bind should be performed.
 * @param endpoint The IP address and port number to bind to.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_server_socket_bind(struct cio_server_socket *ss, const struct cio_inet_socket_address *endpoint);

/**
 * @brief Sets the SO_REUSEADDR socket option.
 *
 * @param ss A pointer to a cio_server_socket for which the socket option should be set.
 * @param on Whether the socket option should be enabled or disabled.
 *
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_server_socket_set_reuse_address(struct cio_server_socket *ss, bool on);

#ifdef __cplusplus
}
#endif

#endif
