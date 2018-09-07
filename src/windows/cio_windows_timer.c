/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2018> <Stephan Gatzka>
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

#include "cio_error_code.h"
#include "cio_timer.h"


static enum cio_error timer_cancel(struct cio_timer *t)
{
	return CIO_SUCCESS;
}

static void timer_close(struct cio_timer *t)
{

}

static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, timer_handler handler, void *handler_context)
{
	return CIO_SUCCESS;
}

enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                              cio_timer_close_hook close_hook)
{
	timer->cancel = timer_cancel;
	timer->close = timer_close;
	timer->expires_from_now = timer_expires_from_now;
	timer->close_hook = close_hook;

	return CIO_SUCCESS;
}
