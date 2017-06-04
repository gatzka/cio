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

/**
 * @brief The type of a close hook function.
 *
 * @param timer The cio_timer the close hook was called on.
 */
typedef void (*cio_timer_close_hook)(struct cio_timer *timer);

/**
 * @brief The type of a timer callback function.
 *
 * @param handler_context The context the functions works on.
 *                        This parameter is fed from @ref cio_timer_expires_from_now_handler_context
 *                        "parameter handler_contex" of @ref cio_timer_expires_from_now "expires_from_now()".
 * @param err If err == ::cio_success, the timer expired.
 *            If err == ::cio_operation_aborted, the timer was @ref cio_timer_cancel "cancelled".
 */
typedef void (*timer_handler)(void *handler_context, enum cio_error err);

struct cio_timer {
	/**
	 * @brief The context pointer which is passed to the functions
	 * specified below.
	 */
	void *context;

	/**
	 * @anchor cio_timer_expires_from_now
	 * @brief Set the timer's expiration time relative to now and arms the timer.
	 *
	 * @param context The cio_timer::context.
	 * @param timeout_ns The expiration time relative to now in nanoseconds.
	 * @param handler The callback function to be called when the timer expires or was cancelled.
	 * @anchor cio_timer_expires_from_now_handler_context
	 * @param handler_context A pointer to a context which might be
	 *                        useful inside @p handler.
	 */
	void (*expires_from_now)(void *context, uint64_t timeout_ns, timer_handler handler, void *handler_context);

	/**
	 * @anchor cio_timer_cancel
	 * @brief Cancels an armed timer.
	 *
	 * @param context The cio_timer::context.
	 * @return ::cio_success for success,
	 *         ::cio_no_such_file_or_directory if the timer wasn't armed.
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
