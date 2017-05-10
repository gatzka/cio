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

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "linux/cio_linux_epoll.h"

static void erase_pending_event(struct cio_linux_eventloop_epoll *loop, const struct cio_linux_event_notifier *ev)
{
	unsigned int i;
	for (i = loop->event_counter + 1; i < loop->num_events; i++) {
		if (loop->epoll_events[i].data.ptr == ev) {
			memmove(&loop->epoll_events[i], &loop->epoll_events[i + 1], (loop->num_events - (i + 1)) * sizeof(loop->epoll_events[0]));
			loop->num_events--;
			break;
		}
	}
}

enum cio_error cio_linux_eventloop_init(struct cio_linux_eventloop_epoll *loop)
{
	loop->epoll_fd = epoll_create(1);
	if (loop->epoll_fd < 0) {
		return errno;
	}

	loop->num_events = 0;
	loop->event_counter = 0;
	loop->go_ahead = true;
	loop->current_ev = NULL;

	return cio_success;
}

void cio_linux_eventloop_destroy(const struct cio_linux_eventloop_epoll *loop)
{
	close(loop->epoll_fd);
}

enum cio_error cio_linux_eventloop_add(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	struct epoll_event epoll_ev;
	ev->registered_events = EPOLLET;

	epoll_ev.data.ptr = ev;
	epoll_ev.events = ev->registered_events;
	if (unlikely(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, ev->fd, &epoll_ev) < 0)) {
		return errno;
	}

	return cio_success;
}

static enum cio_error epoll_mod(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev, uint32_t events)
{
	struct epoll_event epoll_ev;

	epoll_ev.data.ptr = ev;
	epoll_ev.events = events;
	if (unlikely(epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, ev->fd, &epoll_ev) < 0)) {
		return errno;
	}

	return cio_success;
}

enum cio_error cio_linux_eventloop_register_read(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	ev->registered_events |= EPOLLIN;
	return epoll_mod(loop, ev, ev->registered_events);
}

enum cio_error cio_linux_eventloop_unregister_read(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	ev->registered_events &= ~EPOLLIN;
	return epoll_mod(loop, ev, ev->registered_events);
}

enum cio_error cio_linux_eventloop_register_write(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	ev->registered_events |= EPOLLOUT;
	return epoll_mod(loop, ev, ev->registered_events);
}

enum cio_error cio_linux_eventloop_unregister_write(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	ev->registered_events &= ~EPOLLOUT;
	return epoll_mod(loop, ev, ev->registered_events);
}

void cio_linux_eventloop_remove(struct cio_linux_eventloop_epoll *loop, const struct cio_linux_event_notifier *ev)
{
	epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, ev->fd, NULL);
	erase_pending_event(loop, ev);
	if (loop->current_ev == ev) {
		loop->current_ev = NULL;
	}
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

		loop->num_events = (unsigned int)num_events;
		for (loop->event_counter = 0; loop->event_counter < loop->num_events; loop->event_counter++) {
			struct cio_linux_event_notifier *ev = events[loop->event_counter].data.ptr;
			loop->current_ev = ev;

			uint32_t events_type = events[loop->event_counter].events;
			if ((events_type & EPOLLIN) != 0) {
				ev->read_callback(ev->context);
			}

			/*
			 * The current event could be remove via cio_linux_eventloop_remove
			 */
			if ((events_type & EPOLLOUT) != 0) {
				if (likely(loop->current_ev != NULL)) {
					ev->write_callback(ev->context);
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
