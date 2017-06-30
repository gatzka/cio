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
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "fff.h"
#include "unity.h"

#include "cio_buffer_allocator.h"
#include "cio_eventloop.h"
#include "cio_linux_socket.h"
#include "cio_linux_socket_utils.h"
#include "cio_server_socket.h"
#include "cio_socket.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, accept, int, struct sockaddr *, socklen_t *)
void accept_handler(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket);
FAKE_VOID_FUNC(accept_handler, struct cio_server_socket *, void *, enum cio_error, struct cio_socket *)

FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_read, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_write, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, struct cio_eventloop *, const struct cio_event_notifier *)

void on_close(struct cio_server_socket *ss);
FAKE_VOID_FUNC(on_close, struct cio_server_socket *)

FAKE_VALUE_FUNC(int, setsockopt, int, int, int, const void *, socklen_t)
FAKE_VALUE_FUNC(int, bind, int, const struct sockaddr *, socklen_t)
FAKE_VALUE_FUNC(int, listen, int, int)
FAKE_VALUE_FUNC(int, close, int)

struct cio_buffer test_malloc(struct cio_buffer_allocator *context, size_t size);
FAKE_VALUE_FUNC(struct cio_buffer, test_malloc, struct cio_buffer_allocator *, size_t)
void test_free(struct cio_buffer_allocator *context, void *address);
FAKE_VOID_FUNC(test_free, struct cio_buffer_allocator *, void *)

FAKE_VALUE_FUNC0(int, cio_linux_socket_create)

FAKE_VALUE_FUNC(enum cio_error, cio_linux_socket_init, struct cio_socket *, int,
                struct cio_eventloop *,
                struct cio_buffer_allocator *,
                cio_socket_close_hook)

static int optval;

struct cio_buffer_allocator allocator = {
    .alloc = test_malloc,
    .free = test_free};

static struct cio_buffer malloc_success(struct cio_buffer_allocator *context, size_t size)
{
	(void)context;
	struct cio_buffer buffer;
	buffer.address = malloc(size);
	buffer.size = size;
	return buffer;
}

static void free_success(struct cio_buffer_allocator *context, void *address)
{
	(void)context;
	free(address);
}

struct cio_buffer_allocator *cio_get_system_allocator(void)
{
	return &allocator;
}

void setUp(void)
{
	FFF_RESET_HISTORY();
	RESET_FAKE(accept);
	RESET_FAKE(accept_handler);
	RESET_FAKE(cio_linux_socket_create);

	RESET_FAKE(cio_linux_eventloop_add);
	RESET_FAKE(cio_linux_eventloop_remove);
	RESET_FAKE(cio_linux_eventloop_register_read);
	RESET_FAKE(cio_linux_eventloop_register_write);

	RESET_FAKE(on_close);

	RESET_FAKE(setsockopt);
	RESET_FAKE(bind);
	RESET_FAKE(listen);
	RESET_FAKE(close);
	RESET_FAKE(test_malloc);
	RESET_FAKE(test_free);
}

static int listen_fails(int sockfd, int backlog)
{
	(void)sockfd;
	(void)backlog;
	errno = EADDRINUSE;
	return -1;
}

static int setsockopt_fails(int fd, int level, int option_name,
                            const void *option_value, socklen_t option_len)
{
	(void)fd;
	(void)level;
	(void)option_name;
	(void)option_value;
	(void)option_len;

	errno = EINVAL;
	return -1;
}

static int setsockopt_capture(int fd, int level, int option_name,
                              const void *option_value, socklen_t option_len)
{
	(void)fd;
	(void)option_len;
	if ((level == SOL_SOCKET) && (option_name == SO_REUSEADDR)) {
		memcpy(&optval, option_value, sizeof(optval));
	}

	errno = EINVAL;
	return 0;
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

static int accept_wouldblock_second(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	(void)fd;
	(void)addr;
	(void)addrlen;

	if (accept_fake.call_count == 1) {
		return 42;
	} else {
		errno = EWOULDBLOCK;
		return -1;
	}
}

static int accept_fails(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	(void)fd;
	(void)addr;
	(void)addrlen;

	errno = EINVAL;
	return -1;
}

static void accept_handler_close_server_socket(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *sock)
{
	(void)handler_context;
	(void)err;
	if (err == cio_success) {
		struct cio_buffer_allocator *alloc = cio_get_system_allocator();
		alloc->free(alloc, sock);
	}
	ss->close(ss);
}

static void socket_close(struct cio_socket *s)
{
	close(s->ev.fd);
	if (s->close_hook != NULL) {
		s->close_hook(s);
	}
}

static enum cio_error custom_cio_linux_socket_init(struct cio_socket *s, int fd, struct cio_eventloop *loop, struct cio_buffer_allocator *alloc, cio_socket_close_hook hook)
{
	s->ev.fd = fd;
	s->allocator = alloc;

	s->close = socket_close;

	s->loop = loop;
	s->close_hook = hook;
	return cio_success;
}

static void accept_handler_close_socket(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *sock)
{
	(void)handler_context;
	(void)err;
	(void)ss;
	if (err == cio_success) {
		sock->close(sock);
	}
}

static void test_accept_bind_address(void)
{
	accept_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	test_malloc_fake.custom_fake = malloc_success;
	test_free_fake.custom_fake = free_success;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.set_reuse_address(&ss, true);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.bind(&ss, "127.0.0.10", 12345);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.accept(&ss, accept_handler, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_close_in_accept_handler(void)
{
	accept_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	test_malloc_fake.custom_fake = malloc_success;
	test_free_fake.custom_fake = free_success;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), on_close);
	ss.bind(&ss, NULL, 12345);
	ss.accept(&ss, accept_handler, NULL);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_wouldblock(void)
{
	accept_fake.custom_fake = accept_wouldblock;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.bind(&ss, NULL, 12345);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.accept(&ss, accept_handler, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
	ss.close(&ss);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_fails(void)
{
	accept_fake.custom_fake = accept_fails;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), on_close);
	ss.bind(&ss, NULL, 12345);
	ss.accept(&ss, accept_handler, NULL);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_no_handler(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.bind(&ss, NULL, 12345);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.accept(&ss, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_invalid_argument, err);
	ss.close(&ss);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_eventloop_add_fails(void)
{
	cio_linux_eventloop_add_fake.return_val = cio_invalid_argument;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.bind(&ss, NULL, 12345);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.accept(&ss, accept_handler, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
	ss.close(&ss);
}

static void test_init_fails_no_socket(void)
{
	cio_linux_socket_create_fake.return_val = -1;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
}

static void test_init_listen_fails(void)
{
	listen_fake.custom_fake = listen_fails;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.bind(&ss, NULL, 12345);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.accept(&ss, accept_handler, NULL);
	TEST_ASSERT(err != cio_success);
	ss.close(&ss);
}

static void test_init_setsockopt_fails(void)
{
	setsockopt_fake.custom_fake = setsockopt_fails;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), NULL);
	TEST_ASSERT(err == cio_success);
	err = ss.set_reuse_address(&ss, true);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	ss.close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_register_read_fails(void)
{
	cio_linux_eventloop_register_read_fake.return_val = cio_no_space_left_on_device;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.bind(&ss, NULL, 12345);
	TEST_ASSERT_EQUAL(cio_success, err);
	err = ss.accept(&ss, accept_handler, NULL);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	ss.close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_enable_reuse_address(void)
{
	setsockopt_fake.custom_fake = setsockopt_capture;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), NULL);
	TEST_ASSERT(err == cio_success);
	err = ss.set_reuse_address(&ss, true);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, optval);
	TEST_ASSERT_EQUAL(SOL_SOCKET, setsockopt_fake.arg1_val);
	TEST_ASSERT_EQUAL(SO_REUSEADDR, setsockopt_fake.arg2_val);
	TEST_ASSERT_EQUAL(sizeof(int), setsockopt_fake.arg4_val);
	ss.close(&ss);
}

static void test_disable_reuse_address(void)
{
	setsockopt_fake.custom_fake = setsockopt_capture;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), NULL);
	TEST_ASSERT(err == cio_success);
	err = ss.set_reuse_address(&ss, false);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(0, optval);
	TEST_ASSERT_EQUAL(SOL_SOCKET, setsockopt_fake.arg1_val);
	TEST_ASSERT_EQUAL(SO_REUSEADDR, setsockopt_fake.arg2_val);
	TEST_ASSERT_EQUAL(sizeof(int), setsockopt_fake.arg4_val);
	ss.close(&ss);
}

static void test_init_bind_fails(void)
{
	bind_fake.custom_fake = bind_fails;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), NULL);
	TEST_ASSERT(err == cio_success);
	err = ss.bind(&ss, NULL, 12345);
	TEST_ASSERT(err != cio_success);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	ss.close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_accept_malloc_fails(void)
{
	accept_fake.custom_fake = accept_wouldblock_second;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), on_close);
	ss.bind(&ss, NULL, 12345);
	ss.accept(&ss, accept_handler, NULL);

	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_accept_socket_init_fails(void)
{
	accept_fake.custom_fake = accept_wouldblock_second;

	cio_linux_socket_init_fake.return_val = cio_not_enough_memory;
	test_malloc_fake.custom_fake = malloc_success;
	test_free_fake.custom_fake = free_success;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), on_close);
	ss.bind(&ss, NULL, 12345);
	ss.accept(&ss, accept_handler, NULL);

	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(1, test_free_fake.call_count);
}

static void test_accept_socket_close_socket(void)
{
	accept_fake.custom_fake = accept_wouldblock_second;
	accept_handler_fake.custom_fake = accept_handler_close_socket;
	cio_linux_socket_init_fake.custom_fake = custom_cio_linux_socket_init;
	test_malloc_fake.custom_fake = malloc_success;
	test_free_fake.custom_fake = free_success;

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	cio_server_socket_init(&ss, &loop, 5, cio_get_system_allocator(), on_close);
	ss.bind(&ss, NULL, 12345);
	ss.accept(&ss, accept_handler, NULL);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(1, test_malloc_fake.call_count);
	TEST_ASSERT_EQUAL(1, test_free_fake.call_count);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_accept_bind_address);
	RUN_TEST(test_accept_close_in_accept_handler);
	RUN_TEST(test_accept_no_handler);
	RUN_TEST(test_accept_eventloop_add_fails);
	RUN_TEST(test_accept_wouldblock);
	RUN_TEST(test_accept_fails);
	RUN_TEST(test_init_fails_no_socket);
	RUN_TEST(test_init_listen_fails);
	RUN_TEST(test_init_setsockopt_fails);
	RUN_TEST(test_init_bind_fails);
	RUN_TEST(test_accept_malloc_fails);
	RUN_TEST(test_enable_reuse_address);
	RUN_TEST(test_disable_reuse_address);
	RUN_TEST(test_init_register_read_fails);
	RUN_TEST(test_accept_socket_init_fails);
	RUN_TEST(test_accept_socket_close_socket);
	return UNITY_END();
}
