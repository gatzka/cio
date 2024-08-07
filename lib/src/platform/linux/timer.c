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
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "cio/compiler.h"
#include "cio/error_code.h"
#include "cio/eventloop_impl.h"
#include "cio/linux_socket_utils.h"
#include "cio/timer.h"

static const uint64_t NSECONDS_IN_SECONDS = UINT64_C(1000000000);

static struct itimerspec convert_timeoutns_to_itimerspec(uint64_t timeout)
{
	time_t seconds = (time_t)(timeout / NSECONDS_IN_SECONDS);
	long nanos = (long)(timeout - ((uint64_t)seconds * NSECONDS_IN_SECONDS));

	struct itimerspec timespec = {.it_interval.tv_sec = 0, .it_interval.tv_nsec = 0, .it_value.tv_sec = seconds, .it_value.tv_nsec = nanos};
	return timespec;
}

static void timer_read(void *context, enum cio_epoll_error error)
{
	struct cio_timer *timer = context;

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		cio_timer_handler_t handler = timer->handler;
		timer->handler = NULL;
		enum cio_error err = cio_linux_get_socket_error(timer->impl.ev.fd);
		handler(timer, timer->handler_context, err);
		return;
	}

	uint64_t number_of_expirations = 0;

	ssize_t ret = read(timer->impl.ev.fd, &number_of_expirations, sizeof(number_of_expirations));
	if (cio_unlikely(ret == -1)) {
		if (cio_unlikely(errno != EAGAIN)) {
			timer->handler(timer, timer->handler_context, (enum cio_error)(-errno));
		}
	} else {
		cio_timer_handler_t handler = timer->handler;
		timer->handler = NULL;
		handler(timer, timer->handler_context, CIO_SUCCESS);
	}
}

enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                              cio_timer_close_hook_t close_hook)
{
	int fd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
	if (cio_unlikely(fd == -1)) {
		return (enum cio_error)(-errno);
	}

	timer->close_hook = close_hook;
	timer->handler = NULL;
	timer->handler_context = NULL;
	timer->impl.loop = loop;

	timer->impl.ev.read_callback = timer_read;
	timer->impl.ev.write_callback = NULL;
	timer->impl.ev.fd = fd;

	enum cio_error ret_val = cio_linux_eventloop_add(timer->impl.loop, &timer->impl.ev);
	if (cio_unlikely(ret_val != CIO_SUCCESS)) {
		goto eventloop_add_failed;
	}

	ret_val = cio_linux_eventloop_register_read(timer->impl.loop, &timer->impl.ev);
	if (cio_unlikely(ret_val != CIO_SUCCESS)) {
		goto register_read_failed;
	}

	return ret_val;

register_read_failed:
	cio_linux_eventloop_remove(timer->impl.loop, &timer->impl.ev);
eventloop_add_failed:
	close(fd);
	return ret_val;
}

enum cio_error cio_timer_expires_from_now(struct cio_timer *timer, uint64_t timeout_ns, cio_timer_handler_t handler, void *handler_context)
{
	struct itimerspec timeout = convert_timeoutns_to_itimerspec(timeout_ns);

	timer->handler = handler;
	timer->handler_context = handler_context;
	timer->impl.ev.context = timer;

	int ret = timerfd_settime(timer->impl.ev.fd, 0, &timeout, NULL);
	if (cio_unlikely(ret != 0)) {
		return (enum cio_error)(-errno);
	}

	timer_read(timer, CIO_EPOLL_SUCCESS);
	return CIO_SUCCESS;
}

enum cio_error cio_timer_cancel(struct cio_timer *timer)
{
	struct itimerspec timeout;

	if (timer->handler == NULL) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	memset(&timeout, 0x0, sizeof(timeout));
	int ret = timerfd_settime(timer->impl.ev.fd, 0, &timeout, NULL);
	if (cio_likely(ret == 0)) {
		timer->handler(timer, timer->handler_context, CIO_OPERATION_ABORTED);
		timer->handler = NULL;
		return CIO_SUCCESS;
	}

	return (enum cio_error)(-errno);
}

void cio_timer_close(struct cio_timer *timer)
{
	cio_linux_eventloop_remove(timer->impl.loop, &timer->impl.ev);
	if (timer->handler != NULL) {
		cio_timer_cancel(timer);
	}

	close(timer->impl.ev.fd);
	if (timer->close_hook != NULL) {
		timer->close_hook(timer);
	}
}
