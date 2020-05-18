/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2020> <Stephan Gatzka>
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

#include <string.h>
#include <termios.h>

#include "fff.h"
#include "unity.h"

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_uart.h"

DEFINE_FFF_GLOBALS

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

FAKE_VALUE_FUNC(int, tcgetattr, int, struct termios *)
FAKE_VALUE_FUNC(int, tcsetattr, int, int, const struct termios *)
FAKE_VALUE_FUNC(int, cfsetispeed, struct termios *,speed_t)
FAKE_VALUE_FUNC(int, cfsetospeed, struct termios *,speed_t)

FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, struct cio_eventloop *, const struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_read, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_unregister_read, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_write, const struct cio_eventloop *, struct cio_event_notifier *)


static struct termios tty;
static struct cio_eventloop loop;

static int tcgetattr_save(int fd, struct termios *t)
{
	(void)fd;
	*t = tty;
	return 0;
}

static int tcsetattr_save(int fd, int optional_actions, const struct termios *t)
{
	(void)fd;
	(void)optional_actions;

	tty = *t;
	return 0;
}

void setUp(void)
{
	FFF_RESET_HISTORY();
	RESET_FAKE(cfsetispeed)
	RESET_FAKE(cfsetospeed)
	RESET_FAKE(tcgetattr)
	RESET_FAKE(tcsetattr)

	RESET_FAKE(cio_linux_eventloop_add)
	RESET_FAKE(cio_linux_eventloop_register_read)
	RESET_FAKE(cio_linux_eventloop_register_write)
	RESET_FAKE(cio_linux_eventloop_remove)

	memset(&tty, 0x00, sizeof(tty));
	tcgetattr_fake.custom_fake = tcgetattr_save;
	tcsetattr_fake.custom_fake = tcsetattr_save;
}

void tearDown(void)
{
}

static void test_parity(void)
{
	enum cio_uart_parity tests[] = {CIO_UART_PARITY_NONE, CIO_UART_PARITY_ODD, CIO_UART_PARITY_EVEN, CIO_UART_PARITY_MARK, CIO_UART_PARITY_SPACE};
	
	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct cio_uart uart;
		strncpy(uart.impl.name, "/dev/stdout", sizeof(uart.impl.name));
		cio_uart_init(&uart, &loop, NULL);
		enum cio_error err = cio_uart_set_parity(&uart, tests[i]);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Setting parity failed");

		enum cio_uart_parity parity;
		err = cio_uart_get_parity(&uart, &parity);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Getting parity failed");

		TEST_ASSERT_EQUAL_MESSAGE(tests[i], parity, "Get/Set of parity does not match!");
		cio_uart_close(&uart);
	}
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_parity);
	return UNITY_END();
}
