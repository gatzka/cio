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

#ifndef CIO_EVENTLOOP_H
#define CIO_EVENTLOOP_H

#include "cio/error_code.h"
#include "cio/eventloop_impl.h"
#include "cio/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file describes the interface to an eventloop.
 */

/**
 * @brief Prepare all resources of the event loop.
 * Call @ref cio_eventloop_destroy to release all resources.
 *
 * @param loop The eventloop to be initialized.
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_eventloop_init(struct cio_eventloop *loop);

/**
 * @brief Destroy the eventloop an free all underlying resources.
 *
 * @param loop The eventloop to be destroyed.
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT void cio_eventloop_destroy(struct cio_eventloop *loop);
/**
 * @brief Run the eventloop and handle incoming events.
 * 
 * Runs indefinetely until eventlooop is @ref cio_eventloop_cancel "canceled" or a severe error happens.
 *
 * @param loop The eventloop to run.
 * @return ::CIO_SUCCESS for success.
 */
CIO_EXPORT enum cio_error cio_eventloop_run(struct cio_eventloop *loop);

/**
 * @brief Stop execution of eventloop.
 *
 * @ref cio_eventloop_run will return with ::CIO_SUCCESS.
 *
 * @param loop The eventloop that is cancelled.
 */
CIO_EXPORT void cio_eventloop_cancel(struct cio_eventloop *loop);

#ifdef __cplusplus
}
#endif

#endif
