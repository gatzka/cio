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
#include <netinet/tcp.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cio/eventloop.h"
#include "cio/inet_address.h"
#include "cio/linux_socket.h"
#include "cio/linux_socket_utils.h"
#include "cio/server_socket.h"
#include "cio/socket.h"

#include "fff.h"
#include "unity.h"

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

DEFINE_FFF_GLOBALS

void accept_handler(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket);
FAKE_VOID_FUNC(accept_handler, struct cio_server_socket *, void *, enum cio_error, struct cio_socket *)

FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_read, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_write, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, struct cio_eventloop *, const struct cio_event_notifier *)

void on_close(struct cio_server_socket *ss);
FAKE_VOID_FUNC(on_close, struct cio_server_socket *)

FAKE_VALUE_FUNC(int, accept4, int, struct sockaddr *, socklen_t *, int)
FAKE_VALUE_FUNC(int, getsockopt, int, int, int, void *, socklen_t *)
FAKE_VALUE_FUNC(int, setsockopt, int, int, int, const void *, socklen_t)
FAKE_VALUE_FUNC(int, bind, int, const struct sockaddr *, socklen_t)
FAKE_VALUE_FUNC(int, listen, int, int)
FAKE_VALUE_FUNC(int, close, int)
FAKE_VALUE_FUNC(int, socket, int, int, int)
FAKE_VALUE_FUNC(int, unlink, const char *)

struct cio_socket *alloc_client(void);
FAKE_VALUE_FUNC0(struct cio_socket *, alloc_client)
void free_client(struct cio_socket *socket);
FAKE_VOID_FUNC(free_client, struct cio_socket *)

FAKE_VALUE_FUNC(enum cio_error, cio_linux_socket_init, struct cio_socket *, int, struct cio_eventloop *, uint64_t, cio_socket_close_hook_t)

FAKE_VALUE_FUNC(enum cio_error, cio_socket_close, struct cio_socket *)

static int optval;

static struct cio_socket *alloc_success(void)
{
	return malloc(sizeof(struct cio_socket));
}

static struct cio_socket *alloc_fails(void)
{
	return NULL;
}

static void free_success(struct cio_socket *socket)
{
	free(socket);
}

static enum cio_error socket_close(struct cio_socket *s)
{
	close(s->impl.ev.fd);
	if (s->close_hook != NULL) {
		s->close_hook(s);
	}

	return CIO_SUCCESS;
}

void setUp(void)
{
	FFF_RESET_HISTORY()
	RESET_FAKE(accept_handler)
	RESET_FAKE(cio_socket_close)

	RESET_FAKE(cio_linux_eventloop_add)
	RESET_FAKE(cio_linux_eventloop_remove)
	RESET_FAKE(cio_linux_eventloop_register_read)
	RESET_FAKE(cio_linux_eventloop_register_write)

	RESET_FAKE(on_close)

	RESET_FAKE(accept4)
	RESET_FAKE(bind)
	RESET_FAKE(close)
	RESET_FAKE(getsockopt)
	RESET_FAKE(listen)
	RESET_FAKE(setsockopt)
	RESET_FAKE(socket)
	RESET_FAKE(unlink)

	RESET_FAKE(alloc_client)
	RESET_FAKE(free_client)

	cio_socket_close_fake.custom_fake = socket_close;
}

void tearDown(void)
{
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

static int custom_accept_fake(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	(void)fd;
	(void)addr;
	(void)addrlen;
	(void)flags;
	if (accept4_fake.call_count == 1) {
		return 42;
	} else {
		errno = EBADF;
		return -1;
	}
}

static int accept_wouldblock(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	(void)fd;
	(void)addr;
	(void)addrlen;
	(void)flags;

	errno = EAGAIN;
	return -1;
}

static int accept_badf(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	(void)fd;
	(void)addr;
	(void)addrlen;
	(void)flags;

	errno = EBADF;
	return -1;
}

static int accept_wouldblock_second(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	(void)fd;
	(void)addr;
	(void)addrlen;
	(void)flags;

	if (accept4_fake.call_count == 1) {
		return 42;
	} else {
		errno = EAGAIN;
		return -1;
	}
}

static int accept_fails(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	(void)fd;
	(void)addr;
	(void)addrlen;
	(void)flags;

	errno = EINVAL;
	return -1;
}

static void accept_handler_close_server_socket(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *sock)
{
	(void)handler_context;
	(void)err;
	if (err == CIO_SUCCESS) {
		free_success(sock);
	}

	cio_server_socket_close(ss);
}

static enum cio_error custom_cio_linux_socket_init(struct cio_socket *s, int fd, struct cio_eventloop *loop, uint64_t close_timeout_ns, cio_socket_close_hook_t hook)
{
	(void)close_timeout_ns;
	s->impl.ev.fd = fd;

	s->impl.loop = loop;
	s->close_hook = hook;
	return CIO_SUCCESS;
}

static void accept_handler_close_socket(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *sock)
{
	(void)handler_context;
	(void)err;
	(void)ss;
	if (err == CIO_SUCCESS) {
		cio_socket_close(sock);
	}
}

static void fill_inet_socket_address(struct cio_socket_address *endpoint)
{
	cio_init_inet_socket_address(endpoint, cio_get_inet_address_any4(), 12345);
}

static void fill_inet_socket_address_v6(struct cio_socket_address *endpoint)
{
	cio_init_inet_socket_address(endpoint, cio_get_inet_address_any6(), 12345);
}

static void test_accept_error(void)
{
	accept4_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;
	getsockopt_fake.return_val = -1;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	err = cio_server_socket_set_reuse_address(&ss, true);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	err = cio_server_socket_accept(&ss, accept_handler, NULL);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	errno = ENOBUFS;
	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_ERROR);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_NO_BUFFER_SPACE, accept_handler_fake.arg2_val, "accept_hander was not called with correct error code!");
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_bind_address(void)
{
	accept4_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	err = cio_server_socket_set_reuse_address(&ss, true);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	err = cio_server_socket_accept(&ss, accept_handler, NULL);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_close_in_accept_handler(void)
{
	accept4_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	cio_server_socket_accept(&ss, accept_handler, NULL);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_close_in_accept_handler_no_close_hook(void)
{
	accept4_fake.custom_fake = custom_accept_fake;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	cio_server_socket_accept(&ss, accept_handler, NULL);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(0, on_close_fake.call_count);
}

static void test_accept_wouldblock(void)
{
	accept4_fake.custom_fake = accept_wouldblock;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	err = cio_server_socket_accept(&ss, accept_handler, NULL);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_after_close(void)
{
	accept4_fake.custom_fake = accept_badf;
	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	err = cio_server_socket_accept(&ss, accept_handler, NULL);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
}

static void test_accept_fails(void)
{
	accept4_fake.custom_fake = accept_fails;
	accept_handler_fake.custom_fake = accept_handler_close_server_socket;
	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	struct cio_eventloop loop;
	struct cio_server_socket ss;
	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	cio_server_socket_accept(&ss, accept_handler, NULL);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_no_handler(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;
	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	err = cio_server_socket_accept(&ss, NULL, NULL);
	TEST_ASSERT_EQUAL(CIO_INVALID_ARGUMENT, err);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&ss, on_close_fake.arg0_val);
}

static void test_accept_eventloop_add_fails(void)
{
	cio_linux_eventloop_add_fake.return_val = CIO_INVALID_ARGUMENT;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	err = cio_server_socket_accept(&ss, accept_handler, NULL);
	TEST_ASSERT(err != CIO_SUCCESS);
	TEST_ASSERT_EQUAL(0, accept_handler_fake.call_count);
	cio_server_socket_close(&ss);
}

static void test_init_fails_no_socket(void)
{
	socket_fake.return_val = -1;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, CIO_ADDRESS_FAMILY_INET4, alloc_client, free_client, 10, on_close);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket did not failed!");
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
}

static void test_init_listen_fails(void)
{
	listen_fake.custom_fake = listen_fails;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	err = cio_server_socket_accept(&ss, accept_handler, NULL);
	TEST_ASSERT(err != CIO_SUCCESS);
	cio_server_socket_close(&ss);
}

static void test_init_setsockopt_fails(void)
{
	setsockopt_fake.custom_fake = setsockopt_fails;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, CIO_ADDRESS_FAMILY_INET4, alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_set_reuse_address(&ss, true);
	TEST_ASSERT(err != CIO_SUCCESS);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_register_read_fails(void)
{
	cio_linux_eventloop_register_read_fake.return_val = CIO_FILENAME_TOO_LONG;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	err = cio_server_socket_accept(&ss, accept_handler, NULL);
	TEST_ASSERT(err != CIO_SUCCESS);
	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_enable_reuse_address(void)
{
	setsockopt_fake.custom_fake = setsockopt_capture;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, CIO_ADDRESS_FAMILY_INET4, alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_set_reuse_address(&ss, true);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(1, optval);
	TEST_ASSERT_EQUAL(SOL_SOCKET, setsockopt_fake.arg1_val);
	TEST_ASSERT_EQUAL(SO_REUSEADDR, setsockopt_fake.arg2_val);
	TEST_ASSERT_EQUAL(sizeof(int), setsockopt_fake.arg4_val);
	cio_server_socket_close(&ss);
}

static void test_disable_reuse_address(void)
{
	setsockopt_fake.custom_fake = setsockopt_capture;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, CIO_ADDRESS_FAMILY_INET4, alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_set_reuse_address(&ss, false);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);
	TEST_ASSERT_EQUAL(0, optval);
	TEST_ASSERT_EQUAL(SOL_SOCKET, setsockopt_fake.arg1_val);
	TEST_ASSERT_EQUAL(SO_REUSEADDR, setsockopt_fake.arg2_val);
	TEST_ASSERT_EQUAL(sizeof(int), setsockopt_fake.arg4_val);
	cio_server_socket_close(&ss);
}

static void test_enable_tfo(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, CIO_ADDRESS_FAMILY_INET4, alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_set_tcp_fast_open(&ss, true);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_set_tcp_fast_open not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(ss.impl.ev.fd, setsockopt_fake.arg0_val, "fd for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(SOL_TCP, setsockopt_fake.arg1_val, "level for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(TCP_FASTOPEN, setsockopt_fake.arg2_val, "option name for setsockopt not correct!");
}

static void test_disable_tfo(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, CIO_ADDRESS_FAMILY_INET4, alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_set_tcp_fast_open(&ss, false);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_set_tcp_fast_open not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(ss.impl.ev.fd, setsockopt_fake.arg0_val, "fd for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(SOL_TCP, setsockopt_fake.arg1_val, "level for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(TCP_FASTOPEN, setsockopt_fake.arg2_val, "option name for setsockopt not correct!");
}

static void test_tfo_setsockopt_fails(void)
{
	setsockopt_fake.custom_fake = setsockopt_fails;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, CIO_ADDRESS_FAMILY_INET4, alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_set_tcp_fast_open(&ss, false);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_set_tcp_fast_open not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(ss.impl.ev.fd, setsockopt_fake.arg0_val, "fd for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(SOL_TCP, setsockopt_fake.arg1_val, "level for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(TCP_FASTOPEN, setsockopt_fake.arg2_val, "option name for setsockopt not correct!");
}

static void test_accept_bind_ipv6(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	struct cio_socket_address endpoint;
	fill_inet_socket_address_v6(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_accept_bind_abstract_uds(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	struct cio_socket_address endpoint;
	cio_init_uds_socket_address(&endpoint, "\0/tmp/foobar");

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	unlink_fake.return_val = -1;
	errno = ENOENT;

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);

	TEST_ASSERT_EQUAL_MESSAGE(0, unlink_fake.call_count, "unlink was called");
}

static void test_accept_bind_uds_no_stale_file(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	struct cio_socket_address endpoint;
	cio_init_uds_socket_address(&endpoint, "/tmp/foobar");

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	unlink_fake.return_val = -1;
	errno = ENOENT;

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);

	TEST_ASSERT_EQUAL_MESSAGE(1, unlink_fake.call_count, "unlink was called");
}

static void test_accept_bind_uds_stale_file(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	struct cio_socket_address endpoint;
	cio_init_uds_socket_address(&endpoint, "/tmp/foobar");

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	unlink_fake.return_val = 0;

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);

	TEST_ASSERT_EQUAL_MESSAGE(1, unlink_fake.call_count, "unlink was called");
}

static void test_accept_bind_uds_stale_file_cant_remove(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	struct cio_socket_address endpoint;
	cio_init_uds_socket_address(&endpoint, "/tmp/foobar");

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	unlink_fake.return_val = -1;
	errno = EPERM;

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_OPERATION_NOT_PERMITTED, err, "bind did not fail if removal of udsw file failed");

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);

	TEST_ASSERT_EQUAL_MESSAGE(1, unlink_fake.call_count, "unlink was not called");
}

static void test_accept_bind_wrong_family(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	struct cio_socket_address endpoint;
	fill_inet_socket_address_v6(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL(CIO_SUCCESS, err);

	endpoint.impl.sa.socket_address.addr.sa_family = (sa_family_t)CIO_ADDRESS_FAMILY_UNSPEC;

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "bind did not fail if called with an unspecified address family!");

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_bind_fails(void)
{
	bind_fake.custom_fake = bind_fails;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_ADDRESS_IN_USE, err, "call to bind() did not succeed!");

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_bind_no_server_socket(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(NULL, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "call to bind() with no server socket did not fail!");

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_init_bind_no_endpoint(void)
{
	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, CIO_ADDRESS_FAMILY_INET4, alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "call to bind() with no server socket did not fail!");

	TEST_ASSERT_EQUAL(0, close_fake.call_count);
	cio_server_socket_close(&ss);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
}

static void test_accept_malloc_fails(void)
{
	accept4_fake.custom_fake = accept_wouldblock_second;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_fails;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	cio_server_socket_accept(&ss, accept_handler, NULL);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL_MESSAGE(&ss, accept_handler_fake.arg0_val, "first parameter of accept callback is not the server socket struct!");
	TEST_ASSERT_EQUAL_MESSAGE(ss.handler_context, accept_handler_fake.arg1_val, "second parameter of accept callback is not the handler context!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_NO_MEMORY, accept_handler_fake.arg2_val, "third parameter of accept callback is not CIO_NO_MEMORY!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Close was not called!");
}

/*
 * This test accepts an incoming connection, but fails to initalize the new client
 * socket.
 */
static void test_accept_socket_init_fails(void)
{
	accept4_fake.custom_fake = accept_wouldblock_second;

	cio_linux_socket_init_fake.return_val = CIO_INVALID_ARGUMENT;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	cio_server_socket_accept(&ss, accept_handler, NULL);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(1, free_client_fake.call_count);
}

static void test_accept_socket_close_socket(void)
{
	accept4_fake.custom_fake = accept_wouldblock_second;
	accept_handler_fake.custom_fake = accept_handler_close_socket;
	cio_linux_socket_init_fake.custom_fake = custom_cio_linux_socket_init;

	struct cio_eventloop loop;
	struct cio_server_socket ss;

	alloc_client_fake.custom_fake = alloc_success;
	free_client_fake.custom_fake = free_success;

	struct cio_socket_address endpoint;
	fill_inet_socket_address(&endpoint);

	enum cio_error err = cio_server_socket_init(&ss, &loop, 5, cio_socket_address_get_family(&endpoint), alloc_client, free_client, 10, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Initialization of server socket failed!");

	err = cio_server_socket_bind(&ss, &endpoint);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "call to bind() did not succeed!");

	cio_server_socket_accept(&ss, accept_handler, NULL);

	ss.impl.ev.read_callback(ss.impl.ev.context, CIO_EPOLL_SUCCESS);

	TEST_ASSERT_EQUAL(1, accept_handler_fake.call_count);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(1, alloc_client_fake.call_count);
	TEST_ASSERT_EQUAL(1, free_client_fake.call_count);
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_accept_error);
	RUN_TEST(test_accept_bind_address);
	RUN_TEST(test_accept_close_in_accept_handler);
	RUN_TEST(test_accept_close_in_accept_handler_no_close_hook);
	RUN_TEST(test_accept_no_handler);
	RUN_TEST(test_accept_eventloop_add_fails);
	RUN_TEST(test_accept_wouldblock);
	RUN_TEST(test_accept_after_close);
	RUN_TEST(test_accept_fails);
	RUN_TEST(test_init_fails_no_socket);
	RUN_TEST(test_init_listen_fails);
	RUN_TEST(test_init_setsockopt_fails);
	RUN_TEST(test_accept_bind_ipv6);
	RUN_TEST(test_accept_bind_abstract_uds);
	RUN_TEST(test_accept_bind_uds_no_stale_file);
	RUN_TEST(test_accept_bind_uds_stale_file);
	RUN_TEST(test_accept_bind_uds_stale_file_cant_remove);
	RUN_TEST(test_accept_bind_wrong_family);
	RUN_TEST(test_init_bind_fails);
	RUN_TEST(test_init_bind_no_server_socket);
	RUN_TEST(test_init_bind_no_endpoint);
	RUN_TEST(test_accept_malloc_fails);
	RUN_TEST(test_enable_reuse_address);
	RUN_TEST(test_disable_reuse_address);

	RUN_TEST(test_enable_tfo);
	RUN_TEST(test_disable_tfo);
	RUN_TEST(test_tfo_setsockopt_fails);

	RUN_TEST(test_init_register_read_fails);
	RUN_TEST(test_accept_socket_init_fails);
	RUN_TEST(test_accept_socket_close_socket);
	return UNITY_END();
}
