/*
 * SPDX-License-Identifier: MIT
 *
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
#include "cio_util.h"

static void CALLBACK timer_callback(void *context, BOOLEAN fired)
{
	if (fired) {
		struct cio_timer *t = (struct cio_timer *)context;
		BOOL ret = DeleteTimerQueueTimer(NULL, t->impl.ev.overlapped.hEvent, NULL);
		(void)ret; //Deliberately ignore return value. There is nothing we can do if that call fails.
		memset(&t->impl.ev.overlapped, 0, sizeof(t->impl.ev.overlapped));
		PostQueuedCompletionStatus(t->impl.loop->loop_completion_port, 0, (ULONG_PTR)t, &t->impl.ev.overlapped);
	}
}

static void timer_event_callback(struct cio_event_notifier *ev)
{
	struct cio_timer_impl *impl = cio_container_of(ev, struct cio_timer_impl, ev);
	struct cio_timer *t = cio_container_of(impl, struct cio_timer, impl);
	cio_timer_handler handler = t->handler;
	t->handler = NULL;
	handler(t, t->handler_context, CIO_SUCCESS);
}

enum cio_error cio_timer_init(struct cio_timer *timer, struct cio_eventloop *loop,
                              cio_timer_close_hook close_hook)
{
	timer->close_hook = close_hook;
	timer->impl.loop = loop;
	timer->impl.ev.overlapped.hEvent = 0;
	timer->impl.ev.callback = timer_event_callback;

	return CIO_SUCCESS;
}

enum cio_error cio_timer_cancel(struct cio_timer *t)
{
	if (cio_unlikely(t->handler == NULL)) {
		return CIO_OPERATION_NOT_PERMITTED;
	}

	BOOL ret = DeleteTimerQueueTimer(NULL, t->impl.ev.overlapped.hEvent, NULL);
	if (cio_likely(ret)) {
		t->impl.ev.overlapped.hEvent = 0;
		t->handler(t, t->handler_context, CIO_OPERATION_ABORTED);
		t->handler = NULL;
		return CIO_SUCCESS;
	}

	return CIO_INVALID_ARGUMENT;
}

void cio_timer_close(struct cio_timer *t)
{
	if (t->handler != NULL) {
		cio_timer_cancel(t);
	}

	if (t->close_hook != NULL) {
		t->close_hook(t);
	}
}

enum cio_error cio_timer_expires_from_now(struct cio_timer *t, uint64_t timeout_ns, cio_timer_handler handler, void *handler_context)
{
	if (cio_unlikely(t == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	t->handler = handler;
	t->handler_context = handler_context;

	if (t->impl.ev.overlapped.hEvent) {
		BOOL ret = DeleteTimerQueueTimer(NULL, t->impl.ev.overlapped.hEvent, NULL);
		if ((cio_unlikely(ret == FALSE))) {
			return (enum cio_error)(-WSAGetLastError());
		}
	}

	BOOL ret = CreateTimerQueueTimer(&t->impl.ev.overlapped.hEvent, NULL, timer_callback, t, (DWORD)(timeout_ns / 1000000), 0, WT_EXECUTEDEFAULT);
	if (!ret) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_SUCCESS;
}
