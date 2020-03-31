/*
 * SPDX-License-Identifier: MIT
 *
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

#include <errno.h>
#include <kernel.h>
#include <net/socket.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_server_socket.h"
#include "cio_socket.h"

#define STACK_SIZE 2048
static K_THREAD_STACK_ARRAY_DEFINE(stacks, CONFIG_CIO_NUM_SERVER_SOCKETS, STACK_SIZE);

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      enum cio_address_family family,
                                      cio_alloc_client alloc_client,
                                      cio_free_client free_client,
                                      uint64_t close_timeout_ns,
                                      cio_server_socket_close_hook close_hook)
{
	int listen_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (cio_unlikely(listen_fd == -1)) {
		return (enum cio_error)(-errno);
	}

	ss->impl.fd = listen_fd;
	ss->impl.close_timeout_ns = close_timeout_ns;
	ss->impl.loop = loop;

	ss->alloc_client = alloc_client;
	ss->free_client = free_client;
	ss->close_hook = close_hook;
	ss->backlog = (int)backlog;

	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_set_reuse_address(const struct cio_server_socket *ss, bool on)
{
	int reuse;
	if (on) {
		reuse = 1;
	} else {
		reuse = 0;
	}

	if (cio_unlikely(zsock_setsockopt(ss->impl.fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
					sizeof(reuse)) < 0)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_server_socket_bind(struct cio_server_socket *ss, const struct cio_socket_address *endpoint)
{
#if 0
	struct sockaddr addr;

	const char *address = bind_address;
	memset(&addr, 0, sizeof(addr));

	if (address == NULL) {
		// bind to all interfaces
		address = "::";
	}

	if (!net_ipaddr_parse(address, strlen(address), &addr)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct sockaddr *bind_addr;
	size_t addr_size;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;

	if (addr.sa_family == AF_INET) {

		(void)memset(&addr4, 0, sizeof(addr4));
		addr4.sin_family = AF_INET;
		addr4.sin_port = htons(port);
		//if ((bind_address != NULL) && !net_ipv4_is_my_addr(&addr4->sin_addr)) {
		//	return CIO_INVALID_ARGUMENT;
		//}
		printk("bind to ipv4 address: %s port: %u\n", bind_address, port);
		bind_addr = (struct sockaddr *)&addr4;
		addr_size = sizeof(addr4);
	} else {
		(void)memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons(port);
		//if ((bind_address != NULL) && !net_ipv6_is_my_addr(&addr6->sin6_addr)) {
		//	return CIO_INVALID_ARGUMENT;
		//}
		bind_addr = (struct sockaddr *)&addr6;
		addr_size = sizeof(addr6);
	}

	int ret = zsock_bind(ss->impl.fd, bind_addr, addr_size);
	if (cio_unlikely(ret < 0)) {
		return (enum cio_error)(-errno);
	}

#endif
	return CIO_SUCCESS;
}

static void accept_callback(void *context)
{
	printk("In accept callback!\n");
}

static void accept_thread(void *arg1, void *arg2, void *arg3)
{
	struct cio_server_socket *ss = (struct cio_server_socket *)arg1;

	while (true) {
		printk("Before accept!\n");

		if (cio_unlikely(zsock_accept(ss->impl.fd, NULL, NULL) < 0)) {
			printk("Error in accept!\n");
		}

		printk("After accept!\n");
	}
}

enum cio_error cio_server_socket_accept(struct cio_server_socket *ss, cio_accept_handler handler, void *handler_context)
{
	if (cio_unlikely(handler == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	ss->handler = handler;
	ss->handler_context = handler_context;
	ss->impl.ev.callback = accept_callback;
	ss->impl.ev.context = ss;

	if (cio_unlikely(zsock_listen(ss->impl.fd, ss->backlog) < 0)) {
		return (enum cio_error)(-errno);
	}

	k_thread_create(&ss->impl.ipv4_listen_thread, &stacks[0][0], STACK_SIZE,
			accept_thread, ss, 0, 0, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);


	return CIO_SUCCESS;
}

void cio_server_socket_close(struct cio_server_socket *ss)
{
	//cio_zephyr_eventloop_remove_event(&ss->impl.ev);

	printk("zsock_close!\n");
	zsock_close(ss->impl.fd);
	//if (ss->close_hook != NULL) {
	//	ss->close_hook(ss);
	//}

	printk("end of cio_server_socket_close!\n");
}

enum cio_error cio_server_socket_set_tcp_fast_open(const struct cio_server_socket *ss, bool on)
{
	(void)ss;
	(void)on;

	return CIO_OPERATION_NOT_SUPPORTED;
}

