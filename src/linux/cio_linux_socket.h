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

#ifndef CIO_LINUX_SOCKET_H
#define CIO_LINUX_SOCKET_H

#include "cio_socket.h"
#include "linux/cio_linux_epoll.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief The linux specific implementation of a cio_socket.
 */

struct cio_linux_socket;

/**
 * @brief The type of close hook function.
 *
 * @param s The cio_linux_socket the close hook was called on.
 */
typedef void (*cio_linux_socket_close_hook)(struct cio_linux_socket *s);

/**
 * @brief Structure describing a linux socket.
 *
 * All members of this structure shall be considered private
 * and not be used by user of the cio library.
 */
struct cio_linux_socket {
	/**
	 * @privatesection
	 */
	struct cio_socket socket;
	int fd;
	cio_linux_socket_close_hook close;
	struct cio_linux_event_notifier ev;
	struct cio_linux_eventloop_epoll *loop;
};

/**
 * @brief Initializes a cio_linux_socket.
 *
 * @param s The cio_linux_socket that should be initialized.
 * @param loop The event loop the socket shall operate on.
 * @param close A close hook function. If this parameter is non @p NULL,
 * the function will be called directly after
 * @ref cio_socket_close "closing" the cio_socket.
 * It is guaranteed the the cio library will not access any memory of
 * cio_linux_socket that is passed to the close hook. Therefore
 * the hook could be used to free the memory of the linux socket.
 * @return The cio_socket which shall be used after initializing.
 */
struct cio_socket *cio_linux_socket_init(struct cio_linux_socket *s, int client_fd,
                                         struct cio_linux_eventloop_epoll *loop,
                                         cio_linux_socket_close_hook close);

#ifdef __cplusplus
}
#endif

#endif
