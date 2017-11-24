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

#ifndef CIO_HTTP_SERVER_H
#define CIO_HTTP_SERVER_H

#include <stddef.h>
#include <stdint.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_server_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the declarations you need to know if you
 * want to implement an HTTP server.
 *
 * A cio_http_server gives you the ability to @ref cio_http_server_register "register" multiple
 * location handlers which will be instantianted automatically
 * if an HTTP request matches a location.
 *
 * Inside a handler you can specify lots of callback functions like
 * @ref req_handler_on_header_field "on_header_field" or
 * @ref req_handler_on_body "on_body" which will be called automatically
 * when an HTTP request is processed by the HTTP server.
 */

typedef void (*cio_http_serve_error_cb)(struct cio_http_server *server);

typedef struct cio_http_request_handler *(*cio_http_alloc_handler)(const void *config);

struct cio_http_server_location {
	/**
	 * @privatesection
	 */
	const char *path;
	cio_http_alloc_handler alloc_handler;
	struct cio_http_server_location *next;
	const void *config;
};

enum cio_error cio_http_server_location_init(struct cio_http_server_location *location, const char *path, const void *config, cio_http_alloc_handler handler);

struct cio_http_server {
	enum cio_error (*serve)(struct cio_http_server *server);

	/**
	 * @anchor cio_http_server_register
	 */
	enum cio_error (*register_location)(struct cio_http_server *server, struct cio_http_server_location *location);

	/**
	 * @privatesection
	 */
	uint16_t port;
	struct cio_eventloop *loop;
	cio_alloc_client alloc_client;
	cio_free_client free_client;

	uint64_t read_timeout_ns;
	cio_http_serve_error_cb error_cb;
	struct cio_server_socket server_socket;
	struct cio_http_server_location *first_handler;
	size_t num_handlers;
};

/**
 * @brief Initializes an HTTP server.
 * @param server The cio_http_server that should be initialized.
 * @param port The TCP port the HTTP server listens on.
 * @param loop The @ref cio_eventloop "eventloop" the HTTP server uses.
 * @param error_cb This callback function will be called if something goes wrong while the HTTP client connection is established.
 * @param read_timeout_ns The read timeout in nanoseconds.
 * The timeout is started after the HTTP connection is established and canceled after the complete HTTP message was received or after
 * the complete HTTP header was received in case of an upgraded HTTP connection. In case of a timeout the client connection is
 * closed automatically.
 * @anchor cio_http_server_init_alloc_client
 * @param alloc_client A user provided function responsible to allocate a cio_http_client structure.
 * @param free_client A user provided function to free the client memory @ref cio_http_server_init_alloc_client "allocated".
 * @return ::cio_success for success.
 */
enum cio_error cio_http_server_init(struct cio_http_server *server,
                                    uint16_t port,
                                    struct cio_eventloop *loop,
                                    cio_http_serve_error_cb error_cb,
                                    uint64_t read_timeout_ns,
                                    cio_alloc_client alloc_client,
                                    cio_free_client free_client);

#ifdef __cplusplus
}
#endif

#endif
