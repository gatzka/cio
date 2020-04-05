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
#include "cio_zephyr_socket_utils.h"

enum cio_error cio_server_socket_init(struct cio_server_socket *ss,
                                      struct cio_eventloop *loop,
                                      unsigned int backlog,
                                      enum cio_address_family family,
                                      cio_alloc_client alloc_client,
                                      cio_free_client free_client,
                                      uint64_t close_timeout_ns,
                                      cio_server_socket_close_hook close_hook)
{
	struct net_context *context = cio_zephyr_socket_create(family);
	if (cio_unlikely(context == NULL)) {
		return (enum cio_error)(-errno);
	}

	ss->impl.context = context;
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
	(void)ss;
	(void)on;

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_server_socket_bind(struct cio_server_socket *ss, const struct cio_socket_address *endpoint)
{
	if (cio_unlikely((ss == NULL) || (endpoint == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely((enum cio_address_family)endpoint->impl.sa.socket_address.addr.sa_family == CIO_ADDRESS_FAMILY_UNSPEC)) {
		return CIO_INVALID_ARGUMENT;
	}

	const struct sockaddr *addr = &endpoint->impl.sa.socket_address.addr;
	socklen_t addr_len = endpoint->impl.len;

	int ret = net_context_bind(ss->impl.context, addr, addr_len);
	if (cio_unlikely(ret < 0)) {
		return (enum cio_error)(ret);
	}

	return CIO_SUCCESS;
}

static void accept_callback(void *context)
{
	printk("In accept callback!\n");
}

static void net_context_accept_cb(struct net_context *new_context, struct sockaddr *addr, socklen_t addrlen, int status, void *user_data)
{
	struct cio_server_socket *ss = (struct cio_server_socket *)user_data;
	cio_zephyr_eventloop_add_event(ss->impl.loop, &ss->impl.ev);
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

	int ret = net_context_listen(ss->impl.context, ss->backlog);
	if (cio_unlikely(ret < 0)) {
		return (enum cio_error)(ret);
	}

	ret = net_context_accept(ss->impl.context, net_context_accept_cb,  K_NO_WAIT, ss);

	return CIO_SUCCESS;
}

void cio_server_socket_close(struct cio_server_socket *ss)
{
	cio_zephyr_eventloop_remove_event(&ss->impl.ev);

	net_context_put(ss->impl.context);
	if (ss->close_hook != NULL) {
		ss->close_hook(ss);
	}
}

enum cio_error cio_server_socket_set_tcp_fast_open(const struct cio_server_socket *ss, bool on)
{
	(void)ss;
	(void)on;

	return CIO_OPERATION_NOT_SUPPORTED;
}
