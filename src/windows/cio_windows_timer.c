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

#include <Windows.h>

#include "cio_error_code.h"
#include "cio_timer.h"

#include <stdio.h>

static enum cio_error timer_cancel(struct cio_timer *t)
{
	if (t->handler == NULL) {
		return CIO_NO_SUCH_FILE_OR_DIRECTORY;
	}

	BOOL ret = DeleteTimerQueueTimer(NULL, t->ev.event_handle, NULL);
	if (ret) {
		t->handler(t, t->handler_context, CIO_OPERATION_ABORTED);
		return CIO_SUCCESS;
	}

	return CIO_INVALID_ARGUMENT;
}

static void timer_close(struct cio_timer *t)
{
}

static void CALLBACK timer_callback(void *context, BOOLEAN fired)
{
	if (fired) {
		struct cio_timer *t = (struct cio_timer *)context;
		DeleteTimerQueueTimer(NULL, t->ev.event_handle, NULL);
		BOOL ret = PostQueuedCompletionStatus(t->loop->loop_complion_port, 0, t, NULL);

		if (!ret) {
			t->handler(t, t->handler_context, CIO_INVALID_ARGUMENT);
		}
	}
}

static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, timer_handler handler, void *handler_context)
{
	t->handler = handler;
	t->handler_context = handler_context;

	if (t->ev.event_handle) {
		DeleteTimerQueueTimer(NULL, t->ev.event_handle, NULL);
	}

	BOOL ret = CreateTimerQueueTimer(&t->ev.event_handle, NULL, timer_callback, t, (DWORD)(timeout_ns / 1000000), 0, WT_EXECUTEDEFAULT);
	if (!ret) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                              cio_timer_close_hook close_hook)
{
	timer->cancel = timer_cancel;
	timer->close = timer_close;
	timer->expires_from_now = timer_expires_from_now;
	timer->close_hook = close_hook;
	timer->loop = loop;
	timer->ev.event_handle = 0;

	return CIO_SUCCESS;
}
