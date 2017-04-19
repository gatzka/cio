/*
 *The MIT License (MIT)
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
#include <sys/socket.h>
#include <sys/types.h>

#include "fff.h"
#include "unity.h"

#include "cio_linux_epoll.h"
#include "cio_linux_server_socket.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, accept, int, struct sockaddr*, socklen_t*)
void accept_handler(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket);
FAKE_VOID_FUNC(accept_handler, struct cio_server_socket *, void *, enum cio_error, struct cio_socket *)

FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_linux_eventloop_epoll *, struct cio_linux_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, const struct cio_linux_eventloop_epoll *, struct cio_linux_event_notifier *)

void on_close(struct cio_linux_server_socket *ss);
FAKE_VOID_FUNC(on_close, struct cio_linux_server_socket *)

void setUp(void)
{
	FFF_RESET_HISTORY();
	RESET_FAKE(accept);
	RESET_FAKE(accept_handler);
	RESET_FAKE(cio_linux_eventloop_add);
	RESET_FAKE(cio_linux_eventloop_remove);
	RESET_FAKE(on_close);
}

static void close_do_nothing(struct cio_linux_server_socket *ss)
{
	(void)ss;
}

static void close_free(struct cio_linux_server_socket *ss)
{
	free(ss);
}

static int custom_accept_fake(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	(void)fd;
	(void)addr;
	(void)addrlen;
	if (accept_fake.call_count == 1) {
		return 42;
	} else {
		errno = EBADF;
		return -1;
	}
}

static void accept_handler_close_server_socket(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)handler_context;
	(void)err;
	(void)socket;
	ss->close(ss);
}

static void test_accept_close_in_accept_handler(void) {
	accept_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	on_close_fake.custom_fake = close_do_nothing;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, on_close);
	ss->init(ss->context, 12345, 5, NULL);
	ss->accept(ss->context, accept_handler, NULL);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
}

static void test_accept_close_and_free_in_accept_handler(void) {
	accept_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	on_close_fake.custom_fake = close_free;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket *ss_linux = malloc(sizeof(*ss_linux));
	const struct cio_server_socket *ss = cio_linux_server_socket_init(ss_linux, &loop, on_close);
	ss->init(ss->context, 12345, 5, NULL);
	ss->accept(ss->context, accept_handler, NULL);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_accept_close_in_accept_handler);
	RUN_TEST(test_accept_close_and_free_in_accept_handler);
	return UNITY_END();
}
