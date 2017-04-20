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
#include <fcntl.h>
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

FAKE_VALUE_FUNC(int, socket, int, int, int)
FAKE_VALUE_FUNC_VARARG(int, fcntl, int, int, ...)
FAKE_VALUE_FUNC(int, setsockopt, int, int, int, const void *, socklen_t)
FAKE_VALUE_FUNC(int, bind, int, const struct sockaddr *, socklen_t)
FAKE_VALUE_FUNC(int, listen, int, int)
FAKE_VALUE_FUNC(int, close, int)

void setUp(void)
{
	FFF_RESET_HISTORY();
	RESET_FAKE(accept);
	RESET_FAKE(accept_handler);

	RESET_FAKE(cio_linux_eventloop_add);
	RESET_FAKE(cio_linux_eventloop_remove);

	RESET_FAKE(on_close);

	RESET_FAKE(socket);
	RESET_FAKE(fcntl);
	RESET_FAKE(setsockopt);
	RESET_FAKE(bind);
	RESET_FAKE(listen);
	RESET_FAKE(close);
}

static int fcntl_getfl_fails(int fildes, int cmd, va_list ap)
{
	(void)fildes;
	(void)ap;

	if (cmd == F_GETFL){
		errno = EMFILE;
		return -1;
	} else {
		return 0;
	}
}

static int fcntl_setfl_fails(int fildes, int cmd, va_list ap)
{
	(void)fildes;
	(void)ap;

	if (cmd == F_SETFL){
		errno = EMFILE;
		return -1;
	} else {
		return 0;
	}
}

static int socket_fails(int domain, int type, int protocol)
{
	(void)domain;
	(void)type;
	(void)protocol;
	return -1;
}

static int listen_fails(int sockfd, int backlog)
{
	(void)sockfd;
	(void)backlog;
	errno = EADDRINUSE;
	return -1;
}

static int setsockopt_fails(int socket, int level, int option_name,
							const void *option_value, socklen_t option_len)
{
	(void)socket;
	(void)level;
	(void)option_name;
	(void)option_value;
	(void)option_len;

	errno = EINVAL;
	return -1;
}

static int bind_fails(int sockfd, const struct sockaddr *addr,
					  socklen_t addrlen)
{
	(void)sockfd;
	(void)addr;
	(void)addrlen;

	errno = EADDRINUSE;
	return -1;
}

static enum cio_error event_loop_add_fails(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	(void)loop;
	(void)ev;

	return cio_invalid_argument;
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

static int accept_wouldblock(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	(void)fd;
	(void)addr;
	(void)addrlen;

	errno = EWOULDBLOCK;
	return -1;
}

static void accept_handler_close_server_socket(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)handler_context;
	(void)err;
	(void)socket;
	ss->close(ss);
}

static void test_accept_bind_address(void) {
	accept_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	on_close_fake.custom_fake = close_do_nothing;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, on_close);
	ss->init(ss->context, 12345, 5, "127.0.0.1");
	ss->accept(ss->context, accept_handler, NULL);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
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

static void test_accept_wouldblock(void) {
	accept_fake.custom_fake = accept_wouldblock;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	on_close_fake.custom_fake = close_do_nothing;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, on_close);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss->accept(ss->context, accept_handler, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
	ss->close(ss->context);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
}

static void test_accept_close_and_free_in_accept_handler(void) {
	accept_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	on_close_fake.custom_fake = close_free;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket *ss_linux = malloc(sizeof(*ss_linux));
	const struct cio_server_socket *ss = cio_linux_server_socket_init(ss_linux, &loop, on_close);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss->accept(ss->context, accept_handler, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
}

static void test_accept_no_handler(void) {
	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, on_close);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss->accept(ss->context, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_invalid_argument, err);
	ss->close(ss->context);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
}

static void test_accept_eventloop_add_fails(void) {
	cio_linux_eventloop_add_fake.custom_fake = event_loop_add_fails;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, NULL);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss->accept(ss->context, accept_handler, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
	ss->close(ss->context);
}

static void test_init_fails_no_socket(void) {
	socket_fake.custom_fake = socket_fails;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, NULL);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
}

static void test_init_listen_fails(void) {
	listen_fake.custom_fake = listen_fails;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, NULL);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_setsockopt_fails(void) {
	setsockopt_fake.custom_fake = setsockopt_fails;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, NULL);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_bind_fails(void) {
	bind_fake.custom_fake = bind_fails;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, NULL);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_fcntl_getfl_fails(void) {
	fcntl_fake.custom_fake = fcntl_getfl_fails;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, NULL);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_fcntl_setfl_fails(void) {
	fcntl_fake.custom_fake = fcntl_setfl_fails;

	struct cio_linux_eventloop_epoll loop;
	struct cio_linux_server_socket ss_linux;
	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, NULL);
	enum cio_error err = ss->init(ss->context, 12345, 5, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_accept_bind_address);
	RUN_TEST(test_accept_close_in_accept_handler);
	RUN_TEST(test_accept_close_and_free_in_accept_handler);
	RUN_TEST(test_accept_no_handler);
	RUN_TEST(test_accept_eventloop_add_fails);
	RUN_TEST(test_accept_wouldblock);
	RUN_TEST(test_init_fails_no_socket);
	RUN_TEST(test_init_listen_fails);
	RUN_TEST(test_init_setsockopt_fails);
	RUN_TEST(test_init_bind_fails);
	RUN_TEST(test_init_fcntl_getfl_fails);
	RUN_TEST(test_init_fcntl_setfl_fails);
	return UNITY_END();
}
