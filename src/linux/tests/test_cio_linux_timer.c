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
FAKE_VALUE_FUNC(int, close, int)

static int timerfd_create_fails(int clockid, int flags)
{
	(void)clockid;
	(void)flags;

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
	RESET_FAKE(close);
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

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_create_timer);
	RUN_TEST(test_create_timer_timerfd_create_fails);
	RUN_TEST(test_create_timer_eventloop_add_fails);
	RUN_TEST(test_create_timer_eventloop_register_read_fails);
	return UNITY_END();
}
