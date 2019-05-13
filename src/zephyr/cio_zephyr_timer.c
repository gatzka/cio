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

#include <stddef.h>

#include <kernel.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_timer.h"
#include "cio_util.h"

static void timer_callback(void *context)
{
	struct cio_timer *t = context;
	cio_timer_handler handler = t->handler;
	t->handler = NULL;

	enum cio_error err;
	if (t->impl.cancelled) {
		err = CIO_OPERATION_ABORTED;
	} else {
		err = CIO_SUCCESS;
	}

	handler(t, t->handler_context, err);
}

static void expire_or_stop(struct k_timer *work)
{
	struct cio_timer_impl *impl = cio_container_of(work, struct cio_timer_impl, timer);
	struct cio_timer *timer = cio_container_of(impl, struct cio_timer, impl);

	timer->ev.context = timer;
	cio_zephyr_eventloop_add_event(timer->loop, &timer->ev);
}

enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                              cio_timer_close_hook close_hook)
{
	enum cio_error ret_val = CIO_SUCCESS;

	timer->close_hook = close_hook;
	timer->handler = NULL;
	timer->handler_context = NULL;
	timer->loop = loop;
	timer->ev.callback = timer_callback;

	timer->impl.cancelled = false;
	k_timer_init(&timer->impl.timer, expire_or_stop, expire_or_stop);

	return ret_val;
}

enum cio_error cio_timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context)
{
	t->handler = handler;
	t->handler_context = handler_context;
	t->ev.context = t;

	k_timer_start(&t->impl.timer, K_MSEC(timeout_ns / 1000000), 0);

	return CIO_SUCCESS;
}

enum cio_error cio_timer_cancel(struct cio_timer *t)
{
	if (t->handler == NULL) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	t->impl.cancelled = true;
	k_timer_stop(&t->impl.timer);

	return CIO_SUCCESS;
}

void cio_timer_close(struct cio_timer *t)
{
	cio_zephyr_eventloop_remove_event(t->loop, &t->ev);
	if (t->handler != NULL) {
		cio_timer_cancel(t);
	}

	if (t->close_hook != NULL) {
		t->close_hook(t);
	}
}
