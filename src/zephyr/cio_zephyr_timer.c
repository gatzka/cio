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
#include <misc/printk.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_timer.h"
#include "cio_util.h"

static void timer_callback(void *context)
{
	printk("calling timer callback!\n");
}

static void expire(struct k_timer *work)
{
	printk("timer expiration!\n");
	struct cio_timer_impl *impl = cio_container_of(work, struct cio_timer_impl, timer);
	struct cio_timer *timer = cio_container_of(impl, struct cio_timer, impl);
	struct cio_event_notifier ev = {.context = timer, .callback = timer_callback};

	k_msgq_put(&timer->loop->msg_queue, &ev, K_FOREVER);

	// cio_timer_handler handler = timer->handler;
	// timer->handler = NULL;
	// handler(timer, timer->handler_context, CIO_SUCCESS);

	// TODO: put a message into the queue
}

enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                              cio_timer_close_hook close_hook)
{
	enum cio_error ret_val = CIO_SUCCESS;

	timer->close_hook = close_hook;
	timer->handler = NULL;
	timer->handler_context = NULL;
	timer->loop = loop;

	k_timer_init(&timer->impl.timer, expire, NULL);

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
	return CIO_SUCCESS;
}

void cio_timer_close(struct cio_timer *t)
{
}
