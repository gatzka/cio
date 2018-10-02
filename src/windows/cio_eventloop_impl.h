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

#ifndef CIO_WINDOWS_EVENTLOOP_IMPL_H
#define CIO_WINDOWS_EVENTLOOP_IMPL_H

#define WIN32_LEAN_AND_MEAN

#include <stdbool.h>
#include <Windows.h>

#include "cio_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_event_notifier {

	void (*callback)(struct cio_event_notifier *ev, void *context);

	OVERLAPPED overlapped;
	DWORD last_error;
};

struct cio_eventloop {
	/**
	 * @privatesection
	 */
	HANDLE loop_completion_port;
	bool go_ahead;
};

enum cio_error cio_windows_eventloop_add(struct cio_event_notifier *ev, const struct cio_eventloop *loop);
void cio_windows_eventloop_remove(struct cio_event_notifier *ev, const struct cio_eventloop *loop);

enum cio_error cio_windows_add_handle_to_completion_port(HANDLE fd, const struct cio_eventloop *loop, void *context);
struct cio_event_notifier *cio_windows_get_event_entry(void);
void cio_windows_release_event_entry(struct cio_event_notifier *ev);

#ifdef __cplusplus
}
#endif

#endif
