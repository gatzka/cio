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
#include <unistd.h>

#include "fff.h"
#include "unity.h"

#include "cio_error_code.h"
#include "cio_linux_socket.h"
#include "cio_linux_socket_utils.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_read, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_write, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, struct cio_eventloop *, const struct cio_event_notifier *)

FAKE_VALUE_FUNC(int, close, int)

FAKE_VALUE_FUNC0(int, cio_linux_socket_create)

static int socket_create_fails(void)
{
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

	RESET_FAKE(close);

	RESET_FAKE(cio_linux_socket_create);
}

static void test_socket_init(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
}

static void test_socket_init_socket_create_fails(void)
{
	struct cio_socket s;
	cio_linux_socket_create_fake.custom_fake= socket_create_fails;

	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT(err != cio_success);
}

static void test_socket_init_eventloop_add_fails(void)
{
	struct cio_socket s;
	cio_linux_eventloop_add_fake.return_val = cio_invalid_argument;

	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT(err != cio_success);
}

static void test_socket_close_without_hook(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	s.close(&s);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(s.ev.fd, close_fake.arg0_val);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_socket_init);
	RUN_TEST(test_socket_init_socket_create_fails);
	RUN_TEST(test_socket_init_eventloop_add_fails);
	RUN_TEST(test_socket_close_without_hook);
	return UNITY_END();
}
