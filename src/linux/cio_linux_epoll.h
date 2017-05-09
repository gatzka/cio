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

#ifndef CIO_LINUX_EPOLL_H
#define CIO_LINUX_EPOLL_H

#include <stdbool.h>
#include <sys/epoll.h>

#include "cio_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Implementation of an event loop running on Linux using epoll.
 */

/**
 * @private
 */
#define CONFIG_MAX_EPOLL_EVENTS 100

/**
 * @brief Definition of all event types.
 */
enum cio_event_type {
	cio_ev_read = EPOLLIN,  /*!< A read event occured. */
	cio_ev_write = EPOLLOUT /*!< A write event ocured. */
};

/**
 * @brief The cio_linux_event_notifier struct bundles the information
 * necessary to register I/O events.
 */
struct cio_linux_event_notifier {
	/**
	 * @anchor cio_linux_event_notifier_read_callback
	 * @brief The function to be called when a file descriptor becomes readable.
	 */
	void (*read_callback)(void *context);

	/**
	 * @anchor cio_linux_event_notifier_write_callback
	 * @brief The function to be called when a file descriptor becomes readable.
	 */
	void (*write_callback)(void *context);

	/**
	 * @anchor cio_linux_event_notifier_write_callback
	 * @brief The function to be called when a file descriptor becomes readable.
	 */
	void (*error_callback)(void *context);

	/**
	 * @brief The context that is given to the callback functions.
	 */
	void *context;

	/**
	 * @brief The file descriptor that shall be monitored.
	 */
	int fd;
};

struct cio_linux_eventloop_epoll {
	/**
	 * @privatesection
	 */
	int epoll_fd;
	bool go_ahead;
	unsigned int event_counter;
	unsigned int num_events;
	struct epoll_event epoll_events[CONFIG_MAX_EPOLL_EVENTS];
};

enum cio_error cio_linux_eventloop_init(struct cio_linux_eventloop_epoll *loop);
void cio_linux_eventloop_destroy(const struct cio_linux_eventloop_epoll *loop);

enum cio_error cio_linux_eventloop_add(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev);
void cio_linux_eventloop_remove(struct cio_linux_eventloop_epoll *loop, const struct cio_linux_event_notifier *ev);
enum cio_error cio_linux_eventloop_run(struct cio_linux_eventloop_epoll *loop);
void cio_linux_eventloop_cancel(struct cio_linux_eventloop_epoll *loop);

#ifdef __cplusplus
}
#endif

#endif
