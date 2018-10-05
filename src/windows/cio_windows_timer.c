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

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_timer.h"

static enum cio_error timer_cancel(struct cio_timer *t)
{
	if (cio_unlikely(t->handler == NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	BOOL ret = DeleteTimerQueueTimer(NULL, t->ev.overlapped.hEvent, NULL);
	if (cio_likely(ret)) {
		t->handler(t, t->handler_context, CIO_OPERATION_ABORTED);
		t->handler = NULL;
		return CIO_SUCCESS;
	}

	return CIO_INVALID_ARGUMENT;
}

static void timer_close(struct cio_timer *t)
{
	if (t->handler != NULL) {
		timer_cancel(t);
	}

	if (t->close_hook != NULL) {
		t->close_hook(t);
	}
}

static void CALLBACK timer_callback(void *context, BOOLEAN fired)
{
	if (fired) {
		struct cio_timer *t = (struct cio_timer *)context;
		DeleteTimerQueueTimer(NULL, t->ev.overlapped.hEvent, NULL);
		memset(&t->ev.overlapped, 0, sizeof(t->ev.overlapped));
		PostQueuedCompletionStatus(t->loop->loop_completion_port, 0, (ULONG_PTR)t, &t->ev.overlapped);
	}
}

static void timer_event_callback(struct cio_event_notifier *ev, void *context)
{
	(void)ev;
	struct cio_timer *t = (struct cio_timer *)context;
	timer_handler handler = t->handler;
	t->handler = NULL;
	handler(t, t->handler_context, CIO_SUCCESS);
}

static enum cio_error timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, timer_handler handler, void *handler_context)
{
	t->handler = handler;
	t->handler_context = handler_context;

	if (t->ev.overlapped.hEvent) {
		DeleteTimerQueueTimer(NULL, t->ev.overlapped.hEvent, NULL);
	}

	BOOL ret = CreateTimerQueueTimer(&t->ev.overlapped.hEvent, NULL, timer_callback, t, (DWORD)(timeout_ns / 1000000), 0, WT_EXECUTEDEFAULT);
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
	timer->ev.overlapped.hEvent = 0;
	timer->ev.callback = timer_event_callback;

	return CIO_SUCCESS;
}
