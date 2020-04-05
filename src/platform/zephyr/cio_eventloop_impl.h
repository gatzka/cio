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

#ifndef CIO_ZEPHYR_EVENTLOOP_IMPL_H
#define CIO_ZEPHYR_EVENTLOOP_IMPL_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel.h>

#include "cio_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Implementation of an event loop running on Zephyr
 */

/**
 * @brief The cio_event_notifier struct bundles the information
 * necessary to register I/O events.
 */
struct cio_event_notifier {
	void (*callback)(void *context);

	void *context;
	bool removed;
};

struct cio_ev_msg {
	struct cio_event_notifier *ev;
};

#define CIO_ZEPHYR_EVENTLOOP_MSG_QUEUE_SIZE 10

struct cio_eventloop {
	/**
	 * @privatesection
	 */
	struct k_msgq msg_queue;
	char __aligned(4) msg_buf[CIO_ZEPHYR_EVENTLOOP_MSG_QUEUE_SIZE * sizeof(struct cio_ev_msg)];
};

void cio_zephyr_eventloop_add_event(struct cio_eventloop *loop, struct cio_event_notifier *ev);
void cio_zephyr_eventloop_remove_event(struct cio_event_notifier *ev);
void cio_zephyr_ev_init(struct cio_event_notifier *ev);

#ifdef __cplusplus
}
#endif

#endif
