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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <unistd.h>

#include "cio/compiler.h"
#include "cio/error_code.h"
#include "cio/eventloop.h"
#include "cio/eventloop_impl.h"

static void erase_pending_event(struct cio_eventloop *loop, const struct cio_event_notifier *evn)
{
	for (unsigned int i = loop->event_counter + 1; i < loop->num_events; i++) {
		if (loop->epoll_events[i].data.ptr == evn) {
			memmove(&loop->epoll_events[i], &loop->epoll_events[i + 1], (loop->num_events - (i + 1)) * sizeof(loop->epoll_events[0]));
			loop->num_events--;
			break;
		}
	}
}

static enum cio_error epoll_mod(const struct cio_eventloop *loop, struct cio_event_notifier *evn, uint32_t events)
{
	struct epoll_event epoll_ev;

	epoll_ev.data.ptr = evn;
	epoll_ev.events = events;
	if (cio_unlikely(epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, evn->fd, &epoll_ev) < 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_eventloop_init(struct cio_eventloop *loop)
{
	enum cio_error err = CIO_SUCCESS;

	loop->num_events = 0;
	loop->event_counter = 0;
	loop->current_ev = NULL;

	loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (cio_unlikely(loop->epoll_fd == -1)) {
		return (enum cio_error)(-errno);
	}

	loop->stop_ev.fd = eventfd(0, EFD_NONBLOCK);
	if (cio_unlikely(loop->stop_ev.fd == -1)) {
		err = (enum cio_error)(-errno);
		goto eventfd_failed;
	}

	err = cio_linux_eventloop_add(loop, &loop->stop_ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto ev_add_failed;
	}

	err = cio_linux_eventloop_register_read(loop, &loop->stop_ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto ev_register_read_failed;
	}

	return CIO_SUCCESS;

ev_register_read_failed:
	cio_linux_eventloop_remove(loop, &loop->stop_ev);
ev_add_failed:
	close(loop->stop_ev.fd);
eventfd_failed:
	close(loop->epoll_fd);
	return err;
}

void cio_eventloop_destroy(struct cio_eventloop *loop)
{
	cio_linux_eventloop_unregister_read(loop, &loop->stop_ev);
	cio_linux_eventloop_remove(loop, &loop->stop_ev);
	close(loop->stop_ev.fd);
	close(loop->epoll_fd);
}

enum cio_error cio_linux_eventloop_add(const struct cio_eventloop *loop, struct cio_event_notifier *evn)
{
	struct epoll_event epoll_ev;
	evn->registered_events = 0;

	epoll_ev.data.ptr = evn;
	epoll_ev.events = evn->registered_events;
	if (cio_unlikely(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, evn->fd, &epoll_ev) < 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_linux_eventloop_register_read(const struct cio_eventloop *loop, struct cio_event_notifier *evn)
{
	evn->registered_events |= (uint32_t)EPOLLIN;
	return epoll_mod(loop, evn, evn->registered_events);
}

enum cio_error cio_linux_eventloop_unregister_read(const struct cio_eventloop *loop, struct cio_event_notifier *evn)
{
	evn->registered_events &= ~(uint32_t)EPOLLIN;
	return epoll_mod(loop, evn, evn->registered_events);
}

enum cio_error cio_linux_eventloop_register_write(const struct cio_eventloop *loop, struct cio_event_notifier *evn)
{
	evn->registered_events |= (uint32_t)EPOLLOUT;
	return epoll_mod(loop, evn, evn->registered_events);
}

enum cio_error cio_linux_eventloop_unregister_write(const struct cio_eventloop *loop, struct cio_event_notifier *evn)
{
	evn->registered_events &= ~(uint32_t)EPOLLOUT;
	return epoll_mod(loop, evn, evn->registered_events);
}

void cio_linux_eventloop_remove(struct cio_eventloop *loop, const struct cio_event_notifier *evn)
{
	epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, evn->fd, NULL);
	erase_pending_event(loop, evn);
	if (loop->current_ev == evn) {
		loop->current_ev = NULL;
	}
}

static void handle_removed_ev(const struct cio_eventloop *loop, struct cio_event_notifier *evn, uint32_t events_type)
{
	if (cio_likely(loop->current_ev != NULL) && ((events_type & (uint32_t)EPOLLOUT & evn->registered_events) != 0)) {
		enum cio_epoll_error err = CIO_EPOLL_SUCCESS;
		if (cio_unlikely(((events_type & (uint32_t)EPOLLERR) != 0) || ((events_type & (uint32_t)EPOLLHUP) != 0))) {
			err = CIO_EPOLL_ERROR;
		}

		cio_linux_eventloop_unregister_write(loop, evn);
		evn->write_callback(evn->context, err);
	}
}

enum cio_error cio_eventloop_run(struct cio_eventloop *loop)
{
	struct epoll_event *events = loop->epoll_events;

	while (true) {
		int num_events =
		    epoll_wait(loop->epoll_fd, events, CONFIG_MAX_EPOLL_EVENTS, -1);

		if (cio_unlikely(num_events < 0)) {
			if (errno == EINTR) {
				continue;
			}

			return (enum cio_error)(-errno);
		}

		loop->num_events = (unsigned int)num_events;
		for (loop->event_counter = 0; loop->event_counter < loop->num_events; loop->event_counter++) {
			struct cio_event_notifier *evn = events[loop->event_counter].data.ptr;
			if (cio_unlikely(evn == &loop->stop_ev)) {
				goto out;
			}

			uint32_t events_type = events[loop->event_counter].events;
			loop->current_ev = evn;

			if ((events_type & (uint32_t)EPOLLIN & evn->registered_events) != 0) {
				evn->read_callback(evn->context, CIO_EPOLL_SUCCESS);
			}

			handle_removed_ev(loop, evn, events_type);
		}
	}

out:
	return CIO_SUCCESS;
}

void cio_eventloop_cancel(struct cio_eventloop *loop)
{
	uint64_t dummy = 1;
	ssize_t ret = write(loop->stop_ev.fd, &dummy, sizeof(dummy));
	(void)ret;
}
