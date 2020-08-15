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

#ifndef CIO_ZEPHYR_SOCKET_IMPL_H
#define CIO_ZEPHYR_SOCKET_IMPL_H

#include <net/net_context.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cio/cio_error_code.h"
#include "cio/cio_eventloop.h"
#include "cio/cio_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_socket_impl {
	struct cio_event_notifier ev;
	struct cio_timer close_timer;
	uint64_t close_timeout_ns;
	size_t bytes_to_send;
	size_t send_status;
	enum cio_error read_status;
	struct cio_eventloop *loop;
	bool peer_closed_connection;
	struct net_context *context;
};

#ifdef __cplusplus
}
#endif

#endif // CIO_ZEPHYR_SOCKET_IMPL_H
