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

#ifndef CIO_TIMER_H
#define CIO_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the interface of a timer.
 *
 * Currently only one-shot timers are supported, no periodic timer.
 * If you need a periotic timer, you have the rearm the timer in your
 * timer callback.
 */

struct cio_timer;

typedef void (*cio_timer_close_hook)(struct cio_timer *timer);

/**
 * @brief The type of a timer callback function.
 *
 * @param handler_context The context the functions works on.
 * @param err If err == ::cio_success, the timer expired.
 *            If err == ::cio_operation_aborted, the timer was  @ref cio_timer_cancel "cancelled".
 * @param buf A pointer to the begin of the buffer where the data was read in.
 * @param bytes_transferred The number of bytes transferred into @p buf.
 */
typedef void (*timer_handler)(void *handler_context, enum cio_error err);

struct cio_timer {
	void *context;
	void (*expires_from_now)(void *context, uint64_t timeout_ns, timer_handler handler, void *handler_context);

	/**
	 * @anchor cio_timer_cancel
	 * @brief Cancels an armed timer.
	 *
	 * @param context The cio_timer::context.
	 * @return ::cio_success for success.
	 */
	enum cio_error (*cancel)(void *context);
	void (*close)(void *context);

	/**
	 * @privatesection
	 */
	cio_timer_close_hook close_hook;
	timer_handler handler;
	void *handler_context;
	struct cio_event_notifier ev;
	struct cio_eventloop *loop;
};

enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                              cio_timer_close_hook close_hook);

#ifdef __cplusplus
}
#endif

#endif
