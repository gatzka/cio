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

#ifndef CIO_TIMER_H
#define CIO_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the interface of a timer.
 *
 * Currently only one-shot timers are supported, no periodic timer.
 * If you need a periodic timer, you have the rearm the timer in your
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
 * @param timer The timer which called the callback.
 * @param handler_context The context the functions works on.
 *                        This parameter is fed from @ref cio_timer_expires_from_now_handler_context
 *                        "parameter handler_contex" of @ref cio_timer_expires_from_now "expires_from_now()".
 * @param err If err == ::CIO_SUCCESS, the timer expired.
 *            If err == ::CIO_OPERATION_ABORTED, the timer was @ref cio_timer_cancel "cancelled".
 */
typedef void (*cio_timer_handler)(struct cio_timer *timer, void *handler_context, enum cio_error err);

struct cio_timer {
	/**
	 * @privatesection
	 */
	cio_timer_close_hook close_hook;
	cio_timer_handler handler;
	void *handler_context;
	struct cio_event_notifier ev;
	struct cio_eventloop *loop;
};

/**
 * @brief Initializes a cio_timer.
 *
 * @param timer The cio_timer that should be initialized.
 * @param loop The event loop the timer shall operate on.
 * @param close_hook A close hook function. If this parameter is non @c NULL,
 * the function will be called directly after
 * @ref cio_timer_close "closing" the cio_timer.
 * It is guaranteed that the cio library will not access any memory of
 * cio_timer that is passed to the close hook. Therefore,
 * the hook could be used to free the memory of the timer struct.
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                                         cio_timer_close_hook close_hook);

/**
 * @brief Set the timer's expiration time relative to now and arms the timer.
 *
 * @param timer A pointer to a struct cio_timer which shall expire.
 * @param timeout_ns The expiration time relative to "now" in nanoseconds.
 * @param handler The callback function to be called when the timer expires or was cancelled.
 * @anchor cio_timer_expires_from_now_handler_context
 * @param handler_context A pointer to a context which might be
 *                        useful inside @p handler.
 * @return ::CIO_SUCCESS if @p timer was armed successfully.
 */
CIO_EXPORT enum cio_error cio_timer_expires_from_now(struct cio_timer *timer, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context);

/**
 * @brief Cancels an armed timer.
 *
 * @param timer A pointer to a struct cio_timer which shall be canceled.
 * @return ::CIO_SUCCESS for success,
 *         ::CIO_OPERATION_NOT_PERMITTED if the timer wasn't armed.
 */
CIO_EXPORT enum cio_error cio_timer_cancel(struct cio_timer *timer);

/**
 * @brief Closes a timer and frees underlying resources.
 *
 * If the timer is armed and has not expired yet, the timer will be canceled and the timer callback will be called.
 * If a close_hook was given in ::cio_timer_init, the hook is called.
 *
 * @param timer A pointer to a struct cio_timer which shall be closed.
 */
CIO_EXPORT void cio_timer_close(struct cio_timer *timer);

#ifdef __cplusplus
}
#endif

#endif
