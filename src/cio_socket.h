/*
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

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_io_stream.h"

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
 * @brief The type of close hook function.
 *
 * @param s The cio_socket the close hook was called on.
 */
typedef void (*cio_socket_close_hook)(struct cio_socket *s);

struct cio_socket {
	/**
	 * @brief The context pointer which is passed to the functions
	 * specified below.
	 */
	void *context;

	/**
	 * @anchor cio_socket_init
	 * @brief Initializes a cio_socket.
	 *
	 * Creates an unconnected socket.
	 *
	 * @param context The cio_socket::context.
	 *
	 * @return ::cio_success for success.
	 */
	enum cio_error (*init)(void *context);

	/**
	 * @anchor cio_socket_get_io_stream
	 * @brief Gets an I/O stream from the socket.
	 *
	 * @param context The cio_socket::context.
	 *
	 * @return An I/O stream for reading from and writing to this socket.
	 */
	struct cio_io_stream *(*get_io_stream)(void *context);

	/**
	 * @anchor cio_socket_close
	 * @brief Closes the cio_socket.
	 *
	 * Once a socket has been closed, no further communication is possible. Closing the socket
	 * also closes the socket's cio_io_stream.
	 *
	 * @param context The cio_socket::context.
	 */
	void (*close)(void *context);

	/**
	 * @anchor cio_socket_set_tcp_no_delay
	 * @brief Enables/disables the Nagle algorithm
	 *
	 * @param context The cio_socket::context.
	 * @param on Whether Nagle algorithm should be enabled or not.
	 *
	 * @return ::cio_success for success.
	 */
	enum cio_error (*set_tcp_no_delay)(void *context, bool on);

	/**
	 * @anchor cio_socket_set_keep_alive
	 * @brief Enables/disables TCP keepalive messages.
	 *
	 * @param context The cio_socket::context.
	 * @param on Whether or not to enable TCP keepalives.
	 * @param keep_idle_s Time in seconds the connections needs to remain idle
	 *        before start sending keepalive messages. This option might be unused
	 *        in some platform implementations.
	 * @param keep_intvl_s Time in seconds between individual keepalive probes.
	 *        This option might be unused in some platform implementations.
	 * @param keep_cnt The maximum number of keepalive probes before dropping the connection.
	 *        This option might be unused in some platform implementations.
	 *
	 * @return ::cio_success for success.
	 */
	enum cio_error (*set_keep_alive)(void *context, bool on, unsigned int keep_idle_s, unsigned int keep_intvl_s, unsigned int keep_cnt);

	/**
	 * @privatesection
	 */
	struct cio_io_stream stream;
	cio_socket_close_hook close_hook;
	struct cio_event_notifier ev;
	struct cio_eventloop *loop;
};

enum cio_error cio_socket_init(struct cio_socket *s,
                               struct cio_eventloop *loop,
                               cio_socket_close_hook close_hook);

#ifdef __cplusplus
}
#endif

#endif
