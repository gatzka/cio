/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2019> <Stephan Gatzka>
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

#include <kernel.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "zephyr/cio_eventloop_impl.h"

static struct cio_event_notifier stop_ev;

enum cio_error cio_eventloop_init(struct cio_eventloop *loop)
{
	k_msgq_init(&loop->msg_queue, loop->msg_buf, sizeof(struct cio_ev_msg), CIO_ZEPHYR_EVENTLOOP_MSG_QUEUE_SIZE);
	stop_ev.context = &stop_ev;
	return CIO_SUCCESS;
}

void cio_eventloop_destroy(struct cio_eventloop *loop)
{
}

enum cio_error cio_eventloop_run(struct cio_eventloop *loop)
{
	while (true) {
		struct cio_ev_msg msg;
		k_msgq_get(&loop->msg_queue, &msg, K_FOREVER);
		if (cio_likely(msg.ev->context != &stop_ev)) {
			if (cio_likely(!msg.ev->removed)) {
				msg.ev->callback(msg.ev->context);
			}
		} else {
			break;
		}
	}

	return CIO_SUCCESS;
}

void cio_eventloop_cancel(struct cio_eventloop *loop)
{
	k_msgq_purge(&loop->msg_queue);
	cio_zephyr_eventloop_add_event(loop, &stop_ev);
}

void cio_zephyr_eventloop_add_event(struct cio_eventloop *loop, struct cio_event_notifier *ev)
{
	struct cio_ev_msg msg = {.ev = ev};
	k_msgq_put(&loop->msg_queue, &msg, K_FOREVER);
}

void cio_zephyr_eventloop_remove_event(struct cio_event_notifier *ev)
{
	ev->removed = true;
}

void cio_zephyr_ev_init(struct cio_event_notifier *ev)
{
	ev->removed = false;
}
