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

#include <WinSock2.h>
#include <Windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_util.h"
#include "windows/cio_eventloop_impl.h"

struct event_list_entry {
	struct cio_event_notifier ev;
	size_t next_free_idx;
};

static struct event_list_entry *event_list;
static size_t EVENT_LIST_SIZE = 100;
static size_t first_free_event_idx = 0;
static const ULONG_PTR STOP_COMPLETION_KEY = SIZE_MAX;

static enum cio_error create_event_list(void)
{
	event_list = malloc(sizeof(*event_list) * EVENT_LIST_SIZE);
	if (event_list == NULL) {
		return CIO_NO_MEMORY;
	}

	for (size_t i = 0; i < EVENT_LIST_SIZE - 1; i++) {
		event_list[i].next_free_idx = i + 1;
	}

	event_list[EVENT_LIST_SIZE - 1].next_free_idx = SIZE_MAX;
	return CIO_SUCCESS;
}

static struct cio_event_notifier *get_event_entry(void)
{
	if (first_free_event_idx == SIZE_MAX) {
		struct event_list_entry *new_event_list = malloc(sizeof(*event_list) * EVENT_LIST_SIZE * 2);
		if (cio_unlikely(new_event_list == NULL)) {
			return NULL;
		}

		memcpy(new_event_list, event_list, sizeof(event_list) * EVENT_LIST_SIZE);
		for (size_t i = EVENT_LIST_SIZE; i < ((EVENT_LIST_SIZE * 2) - 1); i++) {
			event_list[i].next_free_idx = i + 1;		
		}

		event_list[(EVENT_LIST_SIZE * 2) - 1].next_free_idx = SIZE_MAX;

		first_free_event_idx = EVENT_LIST_SIZE;
		EVENT_LIST_SIZE = EVENT_LIST_SIZE * 2;
		event_list = new_event_list;
	}

	struct event_list_entry *e = &event_list[first_free_event_idx];
	first_free_event_idx = e->next_free_idx;

	return &e->ev;
}

static void release_event_entry(struct cio_event_notifier *ev)
{
	struct event_list_entry *entry = container_of(ev, struct event_list_entry, ev);
	entry->next_free_idx = first_free_event_idx;
	first_free_event_idx = (entry - event_list) / sizeof(*entry);
}

static void destroy_event_list(void)
{
	free(event_list);
}

enum cio_error cio_eventloop_init(struct cio_eventloop *loop)
{
	loop->loop_completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
	if (cio_unlikely(loop->loop_completion_port == NULL)) {
		return (enum cio_error)(-WSAGetLastError());
	}

	WORD RequestedSockVersion = MAKEWORD(2, 2);
	WSADATA wsaData;
	int err = WSAStartup(RequestedSockVersion, &wsaData);
	enum cio_error ret;
	if (cio_unlikely(err != 0)) {
		ret = (enum cio_error)(-WSAGetLastError());
		goto wsa_startup_failed;
	}

	ret = create_event_list();
	if (cio_unlikely(ret != CIO_SUCCESS)) {
		goto malloc_failed;
	}

	loop->go_ahead = true;

	return CIO_SUCCESS;

malloc_failed:
	WSACleanup();
wsa_startup_failed:
	CloseHandle(loop->loop_completion_port);
	return err;
}

void cio_eventloop_destroy(const struct cio_eventloop *loop)
{
	CloseHandle(loop->loop_completion_port);
	WSACleanup();
	destroy_event_list();
}

enum cio_error cio_windows_eventloop_add(struct cio_event_notifier *ev, const struct cio_eventloop *loop)
{
#if 0
	ev->overlapped.hEvent = WSACreateEvent();
	if (cio_unlikely(ev->overlapped.hEvent == WSA_INVALID_EVENT)) {
		return (enum cio_error) - WSAGetLastError();
	}

	if (CreateIoCompletionPort(ev->fd, loop->loop_completion_port, (ULONG_PTR)ev, 1) == NULL) {
		WSACloseEvent(ev->overlapped.hEvent);
		return (enum cio_error) - GetLastError();
	}
#endif

	SecureZeroMemory(&ev->overlapped, sizeof(ev->overlapped));
#if 0
	o.hEvent = WSACreateEvent();
	if (cio_unlikely(o.hEvent == WSA_INVALID_EVENT)) {
		return (enum cio_error)(-WSAGetLastError());
	}
#endif
	if (CreateIoCompletionPort(ev->fd, loop->loop_completion_port, (ULONG_PTR)ev, 1) == NULL) {
		//WSACloseEvent(ev->overlapped.hEvent);
		return (enum cio_error)(-(int)GetLastError());
	}

	ev->last_error = ERROR_SUCCESS;

	return CIO_SUCCESS;
}

void cio_windows_eventloop_remove(struct cio_event_notifier *ev, const struct cio_eventloop *loop)
{
	(void)loop;
	DWORD ret = 0;
	BOOL rc = CancelIoEx(ev->fd, NULL);
	if (rc == FALSE) {
		ret = GetLastError();
	}

	//	WSACloseEvent(ev->overlapped.hEvent);
	ev->overlapped.hEvent = NULL;
}

enum cio_error cio_eventloop_run(struct cio_eventloop *loop)
{
	while (cio_likely(loop->go_ahead)) {
		DWORD size = 0;
		ULONG_PTR completion_key = 0;
		OVERLAPPED *overlapped = NULL;
		BOOL ret = GetQueuedCompletionStatus(loop->loop_completion_port, &size, &completion_key, &overlapped, INFINITE);

		if (cio_unlikely(ret == false)) {
			if (cio_unlikely(overlapped == NULL)) {
				// An unrecoverable error occurred in the completion port. Wait for the next notification.
				continue;
			} else {
				struct cio_event_notifier *ev = (struct cio_event_notifier *)completion_key;
				ev->last_error = GetLastError();
				ev->callback(ev->context);
			}
		}

		if (completion_key == STOP_COMPLETION_KEY) {
			break;
		}

		struct cio_event_notifier *ev = (struct cio_event_notifier *)completion_key;
		DWORD flags = 0;
		if (GetHandleInformation(ev->fd, &flags) == FALSE) {
			DWORD status = GetLastError();
			status++;
		}

		ev->callback(ev->context);
	}

	return CIO_SUCCESS;
}

void cio_eventloop_cancel(struct cio_eventloop *loop)
{
	loop->go_ahead = false;
	PostQueuedCompletionStatus(loop->loop_completion_port, 0, STOP_COMPLETION_KEY, NULL);
}
