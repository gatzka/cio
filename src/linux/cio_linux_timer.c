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
#include <fcntl.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_timer.h"

static const uint64_t NSECONDS_IN_SECONDS = UINT64_C(1000000000);

static struct itimerspec convert_timeoutns_to_itimerspec(uint64_t timeout)
{
	struct itimerspec ts;
	time_t seconds = (time_t)(timeout / NSECONDS_IN_SECONDS);
	long nanos = (long)(timeout - ((uint64_t)seconds * NSECONDS_IN_SECONDS));

	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	ts.it_value.tv_sec = seconds;
	ts.it_value.tv_nsec = nanos;
	return ts;
}

static void timer_read(void *context)
{
	struct cio_timer *t = context;
	uint64_t number_of_expirations;

	ssize_t ret = read(t->ev.fd, &number_of_expirations, sizeof(number_of_expirations));
	if (cio_unlikely(ret == -1)) {
		if (cio_unlikely((errno != EAGAIN) && (errno != EWOULDBLOCK))) {
			t->handler(t, t->handler_context, (enum cio_error)(-errno));
		}
	} else {
		cio_timer_handler handler = t->handler;
		t->handler = NULL;
		handler(t, t->handler_context, CIO_SUCCESS);
	}
}

enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                              cio_timer_close_hook close_hook)
{
	enum cio_error ret_val;
	int fd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
	if (cio_unlikely(fd == -1)) {
		return (enum cio_error)(-errno);
	}

	timer->close_hook = close_hook;
	timer->handler = NULL;
	timer->handler_context = NULL;
	timer->loop = loop;

	timer->ev.read_callback = timer_read;
	timer->ev.error_callback = NULL;
	timer->ev.write_callback = NULL;
	timer->ev.fd = fd;

	ret_val = cio_linux_eventloop_add(timer->loop, &timer->ev);
	if (cio_unlikely(ret_val != CIO_SUCCESS)) {
		goto eventloop_add_failed;
	}

	ret_val = cio_linux_eventloop_register_read(timer->loop, &timer->ev);
	if (cio_unlikely(ret_val != CIO_SUCCESS)) {
		goto register_read_failed;
	}

	return ret_val;

register_read_failed:
	cio_linux_eventloop_remove(timer->loop, &timer->ev);
eventloop_add_failed:
	close(fd);
	return ret_val;
}

enum cio_error cio_timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context)
{
	struct itimerspec timeout = convert_timeoutns_to_itimerspec(timeout_ns);
	int ret;

	t->handler = handler;
	t->handler_context = handler_context;
	t->ev.context = t;

	ret = timerfd_settime(t->ev.fd, 0, &timeout, NULL);
	if (cio_unlikely(ret != 0)) {
		return (enum cio_error)(-errno);
	}

	timer_read(t);
	return CIO_SUCCESS;
}

enum cio_error cio_timer_cancel(struct cio_timer *t)
{
	struct itimerspec timeout;
	int ret;

	if (t->handler == NULL) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	memset(&timeout, 0x0, sizeof(timeout));
	ret = timerfd_settime(t->ev.fd, 0, &timeout, NULL);
	if (cio_likely(ret == 0)) {
		t->handler(t, t->handler_context, CIO_OPERATION_ABORTED);
		t->handler = NULL;
		return CIO_SUCCESS;
	}

	return (enum cio_error)(-errno);
}

void cio_timer_close(struct cio_timer *t)
{
	cio_linux_eventloop_remove(t->loop, &t->ev);
	if (t->handler != NULL) {
		cio_timer_cancel(t);
	}

	close(t->ev.fd);
	if (t->close_hook != NULL) {
		t->close_hook(t);
	}
}
