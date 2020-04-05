/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2020> <Stephan Gatzka>
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

#include <net/net_context.h>
#include <stdint.h>

#include "cio_compiler.h"
#include "cio_endian.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_io_stream.h"
#include "cio_read_buffer.h"
#include "cio_socket.h"
#include "cio_socket_address.h"
#include "cio_timer.h"
#include "cio_util.h"
#include "cio_write_buffer.h"
#include "cio_zephyr_socket.h"

struct cio_io_stream *cio_socket_get_io_stream(struct cio_socket *socket)
{
	return &socket->stream;
}

enum cio_error cio_zephyr_socket_init(struct cio_socket *s, struct net_context *net_context, struct cio_eventloop *loop, uint64_t close_timeout_ns, cio_socket_close_hook close_hook)
{
	return CIO_SUCCESS;
}