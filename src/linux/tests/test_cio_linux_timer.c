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

#include <errno.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "fff.h"
#include "unity.h"

#include "cio_error_code.h"
#include "cio_timer.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_read, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_write, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, struct cio_eventloop *, const struct cio_event_notifier *)

FAKE_VALUE_FUNC(int, timerfd_create, int, int)
FAKE_VALUE_FUNC(int, timerfd_settime, int, int, const struct itimerspec *, struct itimerspec *)
FAKE_VALUE_FUNC(int, close, int)

void on_close(struct cio_timer *timer);
FAKE_VOID_FUNC(on_close, struct cio_timer *)

void handle_timeout(struct cio_timer *context, void *handler_context, enum cio_error err);
FAKE_VOID_FUNC(handle_timeout, struct cio_timer *, void *, enum cio_error)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static int timerfd_create_fails(int clockid, int flags)
{
	(void)clockid;
	(void)flags;

	errno = EINVAL;
	return -1;
}

static int settime_ok(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value)
{
	(void)fd;
	(void)flags;
	(void)new_value;
	(void)old_value;

	return 0;
}

static int settime_fails(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value)
{
	(void)fd;
	(void)flags;
	(void)new_value;
	(void)old_value;

	errno = EINVAL;
	return -1;
}

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(cio_linux_eventloop_add);
	RESET_FAKE(cio_linux_eventloop_remove);
	RESET_FAKE(cio_linux_eventloop_register_read);
	RESET_FAKE(cio_linux_eventloop_register_write);

	RESET_FAKE(timerfd_create);
	RESET_FAKE(timerfd_settime);
	RESET_FAKE(close);

	RESET_FAKE(on_close);
	RESET_FAKE(handle_timeout);
}

static void test_create_timer(void)
{
	timerfd_create_fake.return_val = 1;

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, cio_linux_eventloop_add_fake.call_count);
	TEST_ASSERT_EQUAL(1, cio_linux_eventloop_register_read_fake.call_count);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	TEST_ASSERT_EQUAL(0, cio_linux_eventloop_remove_fake.call_count);
}

static void test_create_timer_timerfd_create_fails(void)
{
	timerfd_create_fake.custom_fake = timerfd_create_fails;

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(0, cio_linux_eventloop_add_fake.call_count);
	TEST_ASSERT_EQUAL(0, cio_linux_eventloop_register_read_fake.call_count);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	TEST_ASSERT_EQUAL(0, cio_linux_eventloop_remove_fake.call_count);
}

static void test_create_timer_eventloop_add_fails(void)
{
	static const int timerfd = 5;
	timerfd_create_fake.return_val = timerfd;
	cio_linux_eventloop_add_fake.return_val = cio_invalid_argument;

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(1, cio_linux_eventloop_add_fake.call_count);
	TEST_ASSERT_EQUAL(0, cio_linux_eventloop_register_read_fake.call_count);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(timerfd, close_fake.arg0_val);
	TEST_ASSERT_EQUAL(0, cio_linux_eventloop_remove_fake.call_count);
}

static void test_create_timer_eventloop_register_read_fails(void)
{
	static const int timerfd = 5;
	timerfd_create_fake.return_val = timerfd;
	cio_linux_eventloop_register_read_fake.return_val = cio_invalid_argument;

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(1, cio_linux_eventloop_add_fake.call_count);
	TEST_ASSERT_EQUAL(1, cio_linux_eventloop_register_read_fake.call_count);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(timerfd, close_fake.arg0_val);
	TEST_ASSERT_EQUAL(1, cio_linux_eventloop_remove_fake.call_count);
}

static void test_close_without_hook(void)
{
	static const int timerfd = 5;
	timerfd_create_fake.return_val = timerfd;

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	timer.close(&timer);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(timerfd, close_fake.arg0_val);
}

static void test_close_with_hook(void)
{
	static const int timerfd = 5;
	timerfd_create_fake.return_val = timerfd;

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	timer.close(&timer);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(timerfd, close_fake.arg0_val);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&timer, on_close_fake.arg0_val);
}

static void test_close_while_armed(void)
{
	static const int timerfd = 5;
	timerfd_create_fake.return_val = timerfd;

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	timer.expires_from_now(&timer, 2000, handle_timeout, NULL);

	timer.close(&timer);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(timerfd, close_fake.arg0_val);

	TEST_ASSERT_EQUAL(1, handle_timeout_fake.call_count);
	TEST_ASSERT_EQUAL(cio_operation_aborted, handle_timeout_fake.arg2_val);
	TEST_ASSERT_EQUAL(&timer, handle_timeout_fake.arg0_val);
}

static void test_cancel_without_arming(void)
{
	static const int timerfd = 5;
	timerfd_create_fake.return_val = timerfd;

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	err = timer.cancel(&timer);
	TEST_ASSERT(err != cio_success);
}

static void test_cancel_settime_in_cancel_fails(void)
{
	static const int timerfd = 5;
	timerfd_create_fake.return_val = timerfd;

	int (*custom_fakes[])(int, int, const struct itimerspec *, struct itimerspec *) =
	    {settime_ok,
	     settime_fails};
	SET_CUSTOM_FAKE_SEQ(timerfd_settime, custom_fakes, ARRAY_SIZE(custom_fakes));

	struct cio_timer timer;
	enum cio_error err = cio_timer_init(&timer, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	timer.expires_from_now(&timer, 2000, handle_timeout, NULL);

	err = timer.cancel(&timer);
	TEST_ASSERT(err == cio_success);
	TEST_ASSERT_EQUAL(1, handle_timeout_fake.call_count);
	TEST_ASSERT((handle_timeout_fake.arg2_val != cio_success) && (handle_timeout_fake.arg2_val != cio_operation_aborted));
	TEST_ASSERT_EQUAL(&timer, handle_timeout_fake.arg0_val);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_create_timer);
	RUN_TEST(test_create_timer_timerfd_create_fails);
	RUN_TEST(test_create_timer_eventloop_add_fails);
	RUN_TEST(test_create_timer_eventloop_register_read_fails);
	RUN_TEST(test_close_without_hook);
	RUN_TEST(test_close_with_hook);
	RUN_TEST(test_close_while_armed);
	RUN_TEST(test_cancel_without_arming);
	RUN_TEST(test_cancel_settime_in_cancel_fails);
	return UNITY_END();
}
