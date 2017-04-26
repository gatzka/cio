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

#include <sys/epoll.h>

#include "fff.h"
#include "unity.h"

#include "cio_linux_epoll.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, epoll_create, int)
FAKE_VALUE_FUNC(int, epoll_ctl, int, int, int, struct epoll_event *)
FAKE_VALUE_FUNC(int, epoll_wait, int, struct epoll_event *, int, int)
FAKE_VALUE_FUNC(int, close, int)

FAKE_VOID_FUNC(epoll_callback, void *);

static unsigned int events_in_list = 0;
static struct cio_linux_event_notifier *ev[100];

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(epoll_create);
	RESET_FAKE(epoll_ctl);
	RESET_FAKE(epoll_wait);
	RESET_FAKE(close);
	RESET_FAKE(epoll_callback);
	events_in_list = 0;
}

static int epoll_create_fail(int size)
{
	(void)size;

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

static int epoll_ctl_save(int epfd, int op, int fd, struct epoll_event *event)
{
	(void)epfd;
	(void)fd;

	if (op == EPOLL_CTL_ADD) {
		ev[events_in_list++] = event->data.ptr;
	}

	return 0;
}

static int notify_single_fd(int epfd, struct epoll_event *events,
                            int maxevents, int timeout)
{
	(void)epfd;
	(void)maxevents;
	(void)timeout;

	if (epoll_wait_fake.call_count == 1) {
		events->events = EPOLLIN;
		events->data.ptr = ev[0];
		return 1;
	} else {
		return -1;
	}
}

static void test_create_loop(void)
{
	struct cio_linux_eventloop_epoll loop;
	enum cio_error err = cio_linux_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, epoll_create_fake.call_count);

	cio_linux_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_create_loop_fails(void)
{
	epoll_create_fake.custom_fake = epoll_create_fail;

	struct cio_linux_eventloop_epoll loop;
	enum cio_error err = cio_linux_eventloop_init(&loop);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(1, epoll_create_fake.call_count);
}

static void test_add_event(void)
{
	static const int fake_fd = 42;
	struct cio_linux_eventloop_epoll loop;
	enum cio_error err = cio_linux_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, epoll_create_fake.call_count);

	struct cio_linux_event_notifier ev;
	ev.fd = fake_fd;
	ev.callback = NULL;
	ev.context = NULL;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_linux_eventloop_remove(&loop, &ev);
	TEST_ASSERT_EQUAL(2, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_DEL, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	cio_linux_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_cancel(void)
{
	struct cio_linux_eventloop_epoll loop;
	enum cio_error err = cio_linux_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, epoll_create_fake.call_count);

	cio_linux_eventloop_cancel(&loop);
	err = cio_linux_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(cio_success, err);

	cio_linux_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_add_event_fails(void)
{
	epoll_ctl_fake.custom_fake = epoll_ctl_fail;
	static const int fake_fd = 42;
	struct cio_linux_eventloop_epoll loop;
	enum cio_error err = cio_linux_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, epoll_create_fake.call_count);

	struct cio_linux_event_notifier ev;
	ev.fd = fake_fd;
	ev.callback = NULL;
	ev.context = NULL;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT(err != cio_success);

	cio_linux_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_notify_event(void)
{
	epoll_wait_fake.custom_fake = notify_single_fd;
	epoll_ctl_fake.custom_fake = epoll_ctl_save;

	struct cio_linux_eventloop_epoll loop;
	enum cio_error err = cio_linux_eventloop_init(&loop);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, epoll_create_fake.call_count);

	static const int fake_fd = 42;
	struct cio_linux_event_notifier ev;
	ev.fd = fake_fd;
	ev.callback = epoll_callback;
	ev.context = &loop;
	err = cio_linux_eventloop_add(&loop, &ev);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, epoll_ctl_fake.call_count);
	TEST_ASSERT_EQUAL(loop.epoll_fd, epoll_ctl_fake.arg0_val);
	TEST_ASSERT_EQUAL(EPOLL_CTL_ADD, epoll_ctl_fake.arg1_val);
	TEST_ASSERT_EQUAL(fake_fd, epoll_ctl_fake.arg2_val);

	err = cio_linux_eventloop_run(&loop);
	TEST_ASSERT_EQUAL(1, epoll_callback_fake.call_count);
	TEST_ASSERT_EQUAL(&loop, epoll_callback_fake.arg0_val);

	cio_linux_eventloop_destroy(&loop);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_create_loop);
	RUN_TEST(test_create_loop_fails);
	RUN_TEST(test_add_event);
	RUN_TEST(test_add_event_fails);
	RUN_TEST(test_cancel);
	RUN_TEST(test_notify_event);
	return UNITY_END();
}
