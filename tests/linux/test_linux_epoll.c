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

#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "cio/eventloop.h"

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, epoll_create1, int)

extern int eventfd(unsigned int initval, int flags);
FAKE_VALUE_FUNC(int, eventfd, unsigned int, int)
FAKE_VALUE_FUNC(int, epoll_ctl, int, int, int, struct epoll_event *)
FAKE_VALUE_FUNC(int, epoll_wait, int, struct epoll_event *, int, int)
FAKE_VALUE_FUNC(int, close, int)
FAKE_VALUE_FUNC(ssize_t, write, int, const void *, size_t)

void epoll_callback(void *, int);
FAKE_VOID_FUNC(epoll_callback, void *, int)
void epoll_callback_second_fd(void *, enum cio_epoll_error);
FAKE_VOID_FUNC(epoll_callback_second_fd, void *, enum cio_epoll_error)
void epoll_callback_third_fd(void *, enum cio_epoll_error);
FAKE_VOID_FUNC(epoll_callback_third_fd, void *, enum cio_epoll_error)
void epoll_callback_forth_fd(void *, enum cio_epoll_error);
FAKE_VOID_FUNC(epoll_callback_forth_fd, void *, enum cio_epoll_error)
void epoll_callback_remove_third_fd(void *, enum cio_epoll_error);
FAKE_VOID_FUNC(epoll_callback_remove_third_fd, void *, enum cio_epoll_error)
void epoll_callback_remove_loop(void *, enum cio_epoll_error);
FAKE_VOID_FUNC(epoll_callback_remove_loop, void *, enum cio_epoll_error)
void epoll_callback_unregister_read_second_fd(void *, enum cio_epoll_error);
FAKE_VOID_FUNC(epoll_callback_unregister_read_second_fd, void *, enum cio_epoll_error)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static unsigned int events_in_list = 0;
static struct cio_event_notifier *(event_list[100]);

void setUp(void)
{
	FFF_RESET_HISTORY()

	RESET_FAKE(epoll_create1)
	RESET_FAKE(eventfd)
	RESET_FAKE(epoll_ctl)
	RESET_FAKE(epoll_wait)
	RESET_FAKE(close)
	RESET_FAKE(epoll_callback)
	RESET_FAKE(epoll_callback_second_fd)
	RESET_FAKE(epoll_callback_third_fd)
	RESET_FAKE(epoll_callback_forth_fd)
	RESET_FAKE(epoll_callback_remove_third_fd)
	RESET_FAKE(epoll_callback_remove_loop)
	RESET_FAKE(epoll_callback_unregister_read_second_fd)
	events_in_list = 0;
}

void tearDown(void)
{
}

static void remove_third_fd(void *context, enum cio_epoll_error error)
{
	(void)error;
	struct cio_eventloop *loop = context;
	cio_linux_eventloop_remove(loop, event_list[2]);
}

static void remove_from_loop(void *context, enum cio_epoll_error error)
{
	(void)error;
	struct cio_eventloop *loop = context;
	struct cio_event_notifier *ev = event_list[0];
	cio_linux_eventloop_remove(loop, ev);
	free(ev);
}

static void unregister_write_event(void *context, enum cio_epoll_error error)
{
	(void)error;
	struct cio_eventloop *loop = context;
	struct cio_event_notifier *ev = event_list[0];
	cio_linux_eventloop_unregister_write(loop, ev);
}

static void unregister_read_event(void *context, enum cio_epoll_error err)
{
	(void)err;
	struct cio_eventloop *loop = context;
	struct cio_event_notifier *ev = event_list[1];
	cio_linux_eventloop_unregister_read(loop, ev);
}

static int epoll_create_fail(int size)
{
	(void)size;

	errno = EMFILE;

	return -1;
}

static int eventfd_fail(unsigned int initval, int flags)
{
	(void)initval;
	(void)flags;

	errno = EMFILE;

	return -1;
}

static int epoll_ctl_fail(int epfd, int op, int fd, struct epoll_event *event)
{
	(void)epfd;
	(void)op;
	(void)fd;
	(void)event;

	errno = ENOSPC;

	return -1;
}
static int epoll_ctl_mod_fail(int epfd, int op, int fd, struct epoll_event *event)
{

	(void)epfd;
	(void)fd;
	(void)event;

	if (op == EPOLL_CTL_MOD) {
		errno = ENOSPC;
		return -1;
	} else {
		return 0;
	}
}

static int epoll_ctl_save(int epfd, int op, int fd, struct epoll_event *event)
{
	(void)epfd;
	(void)fd;

	if (op == EPOLL_CTL_ADD) {
		event_list[events_in_list++] = event->data.ptr;
	}

	return 0;
}

static int epoll_ctl_nosave(int epfd, int op, int fd, struct epoll_event *event)
{
	(void)epfd;
	(void)fd;
	(void)op;
	(void)event;

	return 0;
}

static int notify_single_fd(int epfd, struct epoll_event *events,
                            int maxevents, int timeout)
{
	(void)epfd;
	(void)maxevents;
	(void)timeout;

	if (epoll_wait_fake.call_count == 1) {
		events[0].events = EPOLLIN;
		events[0].data.ptr = event_list[0];
		return 1;
	} else {
		return -1;
	}
}

static int notify_no_fd_interrupt(int epfd, struct epoll_event *events,
                                  int maxevents, int timeout)
{
	(void)epfd;
	(void)maxevents;
	(void)timeout;
	(void)events;

	if (epoll_wait_fake.call_count == 1) {
		errno = EINTR;
		return -1;
	} else {
		errno = EINVAL;
		return -1;
	}
}

static int notify_single_fd_multiple_events(int epfd, struct epoll_event *events,
                                            int maxevents, int timeout)
{
	(void)epfd;
	(void)maxevents;
	(void)timeout;

	if (epoll_wait_fake.call_count == 1) {
		events[0].events = EPOLLIN | EPOLLOUT;
		events[0].data.ptr = event_list[0];
		return 1;
	} else {
		return -1;
	}
}

static int notify_four_fds(int epfd, struct epoll_event *events,
                           int maxevents, int timeout)
{
	(void)epfd;
	(void)maxevents;
	(void)timeout;

	if (epoll_wait_fake.call_count == 1) {
		events[0].events = EPOLLIN;
		events[0].data.ptr = event_list[0];
		events[1].events = EPOLLIN;
		events[1].data.ptr = event_list[1];
		events[2].events = EPOLLIN;
		events[2].data.ptr = event_list[2];
		events[3].events = EPOLLIN;
		events[3].data.ptr = event_list[3];
		return 4;
	} else {
		return -1;
	}
}

static int notify_two_fds(int epfd, struct epoll_event *events,
                          int maxevents, int timeout)
{
	(void)epfd;
	(void)maxevents;
	(void)timeout;

	if (epoll_wait_fake.call_count == 1) {
		events[0].events = EPOLLOUT;
		events[0].data.ptr = event_list[0];
		events[1].events = EPOLLIN;
		events[1].data.ptr = event_list[1];
		return 2;
	} else {
		return -1;
	}
}

static int notify_write_and_error(int epfd, struct epoll_event *events,
                          int maxevents, int timeout)
{
	(void)epfd;
	(void)maxevents;
	(void)timeout;

	if (epoll_wait_fake.call_count == 1) {
		events[0].events = EPOLLOUT | EPOLLERR;
		events[0].data.ptr = event_list[0];
		return 1;
	} else {
		return -1;
	}
}

static int notify_write_and_hup(int epfd, struct epoll_event *events,
                          int maxevents, int timeout)
{
	(void)epfd;
	(void)maxevents;
	(void)timeout;

	if (epoll_wait_fake.call_count == 1) {
		events[0].events = EPOLLOUT | EPOLLHUP;
		events[0].data.ptr = event_list[0];
		return 1;
	} else {
		return -1;
	}
}


static void test_create_loop(void)
{
	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_createloop_epoll_create1_fails(void)
{
	epoll_create1_fake.custom_fake = epoll_create_fail;

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT(err != CIO_SUCCESS);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);
}

static void test_createloop_epoll_eventfd_fails(void)
{
	eventfd_fake.custom_fake = eventfd_fail;

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "eventloop_init did not fail");
	TEST_ASSERT_EQUAL_MESSAGE(1, epoll_create1_fake.call_count, "epoll_create1 was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, eventfd_fake.call_count, "eventfd was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "close() was not called");
}

static void test_createloop_epoll_eventloop_add_fails(void)
{
	epoll_ctl_fake.custom_fake = epoll_ctl_fail;

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "eventloop_init did not fail");
	TEST_ASSERT_EQUAL_MESSAGE(1, epoll_create1_fake.call_count, "epoll_create1 was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, eventfd_fake.call_count, "eventfd was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, epoll_ctl_fake.call_count, "epoll_ctl was not called");
	TEST_ASSERT_EQUAL_MESSAGE(2, close_fake.call_count, "close() was not called twice (for epoll fd and stop event fd)");
}

static void test_createloop_epoll_eventloop_register_read_fails(void)
{
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_save, epoll_ctl_fail};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "eventloop_init did not fail");
	TEST_ASSERT_EQUAL_MESSAGE(1, epoll_create1_fake.call_count, "epoll_create1 was not called");
	TEST_ASSERT_EQUAL_MESSAGE(1, eventfd_fake.call_count, "eventfd was not called");
	TEST_ASSERT_EQUAL_MESSAGE(3, epoll_ctl_fake.call_count, "epoll_ctl was not called");
	TEST_ASSERT_EQUAL_MESSAGE(2, close_fake.call_count, "close() was not called twice (for epoll fd and stop event fd)");
}

static void test_add_event(void)
{
	static const int fake_fd = 42;
	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	struct cio_event_notifier ev;
	ev.fd = fake_fd;
	ev.read_callback = NULL;
	ev.context = NULL;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_linux_eventloop_remove(&loop, &ev);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_DEL, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_cancel(void)
{
	epoll_ctl_fake.custom_fake = epoll_ctl_save;
	epoll_wait_fake.custom_fake = notify_single_fd;

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	cio_eventloop_cancel(&loop);
	cio_eventloop_run(&loop);
	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_add_event_fails(void)
{
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_save, epoll_ctl_save, epoll_ctl_fail};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))
	static const int fake_fd = 42;
	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	struct cio_event_notifier ev;
	ev.fd = fake_fd;
	ev.read_callback = NULL;
	ev.context = NULL;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT(err != CIO_SUCCESS);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_register_event_fails(void)
{
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_save, epoll_ctl_save, epoll_ctl_mod_fail};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	static const int fake_fd = 42;
	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	struct cio_event_notifier ev;
	ev.fd = fake_fd;
	ev.read_callback = NULL;
	ev.context = NULL;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	err = cio_linux_eventloop_register_read(&loop, &ev);
	TEST_ASSERT(err != CIO_SUCCESS);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_notify_event(void)
{
	epoll_wait_fake.custom_fake = notify_single_fd;
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_nosave, epoll_ctl_save};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	static const int fake_fd = 42;
	struct cio_event_notifier ev;
	ev.fd = fake_fd;
	ev.read_callback = epoll_callback;
	ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_register_read(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(1, epoll_callback_fake.call_count);
	TEST_ASSERT_EQUAL(&loop, epoll_callback_fake.arg0_val);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EPOLL_SUCCESS, epoll_callback_fake.arg1_val, "callback was not called with success code!");

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_notify_error_event(void)
{
	epoll_wait_fake.custom_fake = notify_write_and_error;
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_nosave, epoll_ctl_save};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	static const int fake_fd = 42;
	struct cio_event_notifier ev;
	ev.fd = fake_fd;
	ev.write_callback = epoll_callback;
	ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_register_write(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(1, epoll_callback_fake.call_count);
	TEST_ASSERT_EQUAL(&loop, epoll_callback_fake.arg0_val);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EPOLL_ERROR, epoll_callback_fake.arg1_val, "callback was not called with error code!");

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_notify_hup_event(void)
{
	epoll_wait_fake.custom_fake = notify_write_and_hup;
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_nosave, epoll_ctl_save};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	static const int fake_fd = 42;
	struct cio_event_notifier ev;
	ev.fd = fake_fd;
	ev.write_callback = epoll_callback;
	ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_register_write(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(1, epoll_callback_fake.call_count);
	TEST_ASSERT_EQUAL(&loop, epoll_callback_fake.arg0_val);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EPOLL_ERROR, epoll_callback_fake.arg1_val, "callback was not called with error code!");

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_epoll_wait_interrupted(void)
{
	epoll_wait_fake.custom_fake = notify_no_fd_interrupt;

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	cio_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(2, epoll_wait_fake.call_count);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_notify_single_fd_multiple_events(void)
{
	epoll_wait_fake.custom_fake = notify_single_fd_multiple_events;
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_nosave, epoll_ctl_save};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	static const int fake_fd = 42;
	struct cio_event_notifier ev;
	ev.fd = fake_fd;
	ev.read_callback = epoll_callback;
	ev.write_callback = epoll_callback;
	ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_register_read(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_register_write(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(5, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(2, epoll_callback_fake.call_count);
	TEST_ASSERT_EQUAL(&loop, epoll_callback_fake.arg0_val);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EPOLL_SUCCESS, epoll_callback_fake.arg1_history[0], "callback was not called with success code!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_EPOLL_SUCCESS, epoll_callback_fake.arg1_history[1], "callback was not called with success code!");

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

/*
 * This test notifies two fds. The first fd gets out event, the second
 * fd an in event. Inside the write callback of the first fd the in event
 * for the second fd is unregistered.
 * The read callback for the second fd must not be called.
 */
static void test_notify_two_fds_unregister_read(void)
{
	epoll_wait_fake.custom_fake = notify_two_fds;
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_nosave, epoll_ctl_save};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	static const int fake_first_fd = 42;
	struct cio_event_notifier first_ev;
	epoll_callback_unregister_read_second_fd_fake.custom_fake = unregister_read_event;
	first_ev.fd = fake_first_fd;
	first_ev.write_callback = epoll_callback_unregister_read_second_fd;
	first_ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &first_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_first_fd, epoll_ctl_fake.arg2_val);
	err = cio_linux_eventloop_register_write(&loop, &first_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_first_fd, epoll_ctl_fake.arg2_val);

	static const int fake_second_fd = 43;
	struct cio_event_notifier second_ev;
	second_ev.fd = fake_second_fd;
	second_ev.read_callback = epoll_callback;
	second_ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &second_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(5, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_second_fd, epoll_ctl_fake.arg2_val);
	err = cio_linux_eventloop_register_read(&loop, &second_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(6, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_second_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(1, epoll_callback_unregister_read_second_fd_fake.call_count);
	TEST_ASSERT_EQUAL(&loop, epoll_callback_unregister_read_second_fd_fake.arg0_val);

	TEST_ASSERT_EQUAL(0, epoll_callback_fake.call_count);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

/*
 * This test notifies a single fd with multiple events (in and out).
 * The read callback removes the event from the eventloop. The write
 * callback must be never called.
 */
static void test_notify_single_fd_multiple_events_remove_from_loop(void)
{
	epoll_wait_fake.custom_fake = notify_single_fd_multiple_events;
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_nosave, epoll_ctl_save};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	epoll_callback_remove_loop_fake.custom_fake = remove_from_loop;

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	static const int fake_fd = 42;
	struct cio_event_notifier *ev = malloc(sizeof(*ev));
	ev->fd = fake_fd;
	ev->read_callback = epoll_callback_remove_loop;
	ev->write_callback = epoll_callback;
	ev->context = &loop;
	err = cio_linux_eventloop_add(&loop, ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_register_read(&loop, ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(1, epoll_callback_remove_loop_fake.call_count);
	TEST_ASSERT_EQUAL(&loop, epoll_callback_remove_loop_fake.arg0_val);

	TEST_ASSERT_EQUAL(0, epoll_callback_fake.call_count);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

/*
 * This test notifies a single fd with multiple events (in and out).
 * The read callback unregisters the write event for that fd.
 * The write callback must not be called afterwards.
 */
static void test_notify_single_fd_multiple_events_unregister_write_event(void)
{
	epoll_wait_fake.custom_fake = notify_single_fd_multiple_events;
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_nosave, epoll_ctl_save};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	static const int fake_fd = 42;
	struct cio_event_notifier ev;
	ev.fd = fake_fd;
	ev.read_callback = unregister_write_event;
	ev.write_callback = epoll_callback;
	ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_register_read(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_register_write(&loop, &ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(5, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_run(&loop);

	TEST_ASSERT_EQUAL(0, epoll_callback_fake.call_count);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

static void test_notify_three_event_and_remove(void)
{
	epoll_wait_fake.custom_fake = notify_four_fds;
	int (*epoll_ctrl_fakes[])(int, int, int, struct epoll_event *) = {epoll_ctl_nosave, epoll_ctl_save};
	SET_CUSTOM_FAKE_SEQ(epoll_ctl, epoll_ctrl_fakes, ARRAY_SIZE(epoll_ctrl_fakes))

	struct cio_eventloop loop;
	enum cio_error err = cio_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, epoll_create1_fake.call_count);

	static const int fake_first_fd = 42;
	struct cio_event_notifier first_ev;
	epoll_callback_remove_third_fd_fake.custom_fake = remove_third_fd;
	first_ev.fd = fake_first_fd;
	first_ev.read_callback = epoll_callback_remove_third_fd;
	first_ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &first_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(3, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_first_fd, epoll_ctl_fake.arg2_val);
	err = cio_linux_eventloop_register_read(&loop, &first_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(4, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_first_fd, epoll_ctl_fake.arg2_val);

	static const int fake_second_fd = 43;
	struct cio_event_notifier second_ev;
	second_ev.fd = fake_second_fd;
	second_ev.read_callback = epoll_callback_second_fd;
	second_ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &second_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_second_fd, epoll_ctl_fake.arg2_val);
	err = cio_linux_eventloop_register_read(&loop, &second_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(6, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_second_fd, epoll_ctl_fake.arg2_val);

	static const int fake_third_fd = 44;
	struct cio_event_notifier third_ev;
	third_ev.fd = fake_third_fd;
	third_ev.read_callback = epoll_callback_third_fd;
	third_ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &third_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_third_fd, epoll_ctl_fake.arg2_val);
	err = cio_linux_eventloop_register_read(&loop, &third_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(8, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_third_fd, epoll_ctl_fake.arg2_val);

	static const int fake_forth_fd = 44;
	struct cio_event_notifier forth_ev;
	forth_ev.fd = fake_forth_fd;
	forth_ev.read_callback = epoll_callback_forth_fd;
	forth_ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &forth_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_forth_fd, epoll_ctl_fake.arg2_val);
	err = cio_linux_eventloop_register_read(&loop, &forth_ev);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(10, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_MOD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_forth_fd, epoll_ctl_fake.arg2_val);

	cio_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(1, epoll_callback_remove_third_fd_fake.call_count);
	TEST_ASSERT_EQUAL(&loop, epoll_callback_remove_third_fd_fake.arg0_val);
	TEST_ASSERT_EQUAL(1, epoll_callback_second_fd_fake.call_count);
	TEST_ASSERT_EQUAL(0, epoll_callback_third_fd_fake.call_count);
	TEST_ASSERT_EQUAL(1, epoll_callback_forth_fd_fake.call_count);

	cio_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(2, close_fake.call_count);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_create_loop);
	RUN_TEST(test_createloop_epoll_create1_fails);
	RUN_TEST(test_createloop_epoll_eventfd_fails);
	RUN_TEST(test_createloop_epoll_eventloop_add_fails);
	RUN_TEST(test_createloop_epoll_eventloop_register_read_fails);
	RUN_TEST(test_add_event);
	RUN_TEST(test_add_event_fails);
	RUN_TEST(test_cancel);
	RUN_TEST(test_register_event_fails);
	RUN_TEST(test_notify_event);
	RUN_TEST(test_notify_error_event);
	RUN_TEST(test_notify_hup_event);
	RUN_TEST(test_notify_three_event_and_remove);
	RUN_TEST(test_notify_single_fd_multiple_events);
	RUN_TEST(test_notify_single_fd_multiple_events_remove_from_loop);
	RUN_TEST(test_notify_single_fd_multiple_events_unregister_write_event);
	RUN_TEST(test_notify_two_fds_unregister_read);
	RUN_TEST(test_epoll_wait_interrupted);
	return UNITY_END();
}
