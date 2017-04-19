/*
 *The MIT License (MIT)
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

#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_linux_epoll.h"

enum cio_error cio_linux_eventloop_init(struct cio_linux_eventloop_epoll *loop)
{
	loop->epoll_fd = epoll_create(1);
	if (loop->epoll_fd < 0) {
		return errno;
	}

	loop->go_ahead = true;

	return cio_success;
}

void cio_linux_eventloop_destroy(const struct cio_linux_eventloop_epoll *loop)
{
	close(loop->epoll_fd);
}

enum cio_error cio_linux_eventloop_add(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	struct epoll_event epoll_ev = {0};

	epoll_ev.data.ptr = ev;
	epoll_ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	if (unlikely(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, ev->fd, &epoll_ev) < 0)) {
		return errno;
	}

	return cio_success;
}

void cio_linux_eventloop_remove(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, ev->fd, NULL);
}

enum cio_error cio_linux_eventloop_run(struct cio_linux_eventloop_epoll *loop)
{
	struct epoll_event *events = loop->epoll_events;

	while (likely(loop->go_ahead)) {
		int num_events =
		    epoll_wait(loop->epoll_fd, events, CONFIG_MAX_EPOLL_EVENTS, -1);

		if (unlikely(num_events < 0)) {
			if (errno == EINTR) {
				continue;
			}

			return errno;
		}

		for (loop->event_counter = 0;  loop->event_counter < (unsigned int)num_events; loop->event_counter++) {
			struct cio_linux_event_notifier *ev = events[loop->event_counter].data.ptr;

			if (unlikely((events[loop->event_counter].events & ~(EPOLLIN | EPOLLOUT)) != 0)) {
				// TODO
			} else {
				if (events[loop->event_counter].events & (EPOLLIN | EPOLLOUT)) {
					ev->callback(ev->context);
				}
			}
		}
	}

	return cio_success;
}

void cio_linux_eventloop_cancel(struct cio_linux_eventloop_epoll *loop)
{
	loop->go_ahead = false;
}
