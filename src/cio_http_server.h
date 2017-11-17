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

#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_read_buffer.h"
#include "cio_server_socket.h"
#include "cio_timer.h"
#include "cio_write_buffer.h"
#include "http-parser/http_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

enum cio_http_status_code {
	cio_http_status_ok = 200,
	cio_http_status_bad_request = 400,
	cio_http_status_not_found = 404,
	cio_http_status_internal_server_error = 500,
};

enum cio_http_method {
	cio_http_delete = HTTP_DELETE,
	cio_http_get = HTTP_GET,
	cio_http_head = HTTP_HEAD,
	cio_http_post = HTTP_POST,
	cio_http_put = HTTP_PUT,
	cio_http_connect = HTTP_CONNECT,
	cio_http_options = HTTP_OPTIONS,
	cio_http_trace = HTTP_TRACE,
	cio_http_copy = HTTP_COPY,
	cio_http_lock = HTTP_LOCK,
	cio_http_mkcol = HTTP_MKCOL,
	cio_http_move = HTTP_MOVE,
	cio_http_propfind = HTTP_PROPFIND,
	cio_http_proppatch = HTTP_PROPPATCH,
	cio_http_search = HTTP_SEARCH,
	cio_http_unlock = HTTP_UNLOCK,
	cio_http_bind = HTTP_BIND,
	cio_http_rebind = HTTP_REBIND,
	cio_http_unbind = HTTP_UNBIND,
	cio_http_acl = HTTP_ACL,
	cio_http_report = HTTP_REPORT,
	cio_http_mkactivity = HTTP_MKACTIVITY,
	cio_http_checkout = HTTP_CHECKOUT,
	cio_http_merge = HTTP_MERGE,
	cio_http_msearch = HTTP_MSEARCH,
	cio_http_notify = HTTP_NOTIFY,
	cio_http_subscribe = HTTP_SUBSCRIBE,
	cio_http_unsubscribe = HTTP_UNSUBSCRIBE,
	cio_http_patch = HTTP_PATCH,
	cio_http_purge = HTTP_PURGE,
	cio_http_mkcalendar = HTTP_MKCALENDAR,
	cio_http_link = HTTP_LINK,
	cio_http_unlink = HTTP_UNLINK
};

enum cio_http_cb_return {
	cio_http_cb_success = 0,
	cio_http_cb_error = -1
};

struct cio_http_client {

	// TODO: is close really necessary for users of the library?
	void (*close)(struct cio_http_client *client);
	void (*write_response)(struct cio_http_client *client, struct cio_write_buffer *wbh);
	void (*write_header)(struct cio_http_client *client, enum cio_http_status_code status);

	struct cio_buffered_stream bs;
	struct cio_read_buffer rb;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb_http_header;

	uint16_t http_major;
	uint16_t http_minor;
	uint64_t content_length;
	enum cio_http_method http_method;

	struct cio_http_request_handler *handler;

	/**
	 * @privatesection
	 */
	struct cio_http_server *server;
	struct cio_socket socket;
	struct cio_timer read_timer;

	bool headers_complete;
	bool to_be_closed;

	http_parser parser;
	http_parser_settings parser_settings;

	size_t buffer_size;
	uint8_t buffer[];
};

typedef enum cio_http_cb_return (*cio_http_cb)(struct cio_http_client *);
typedef enum cio_http_cb_return (*cio_http_data_cb)(struct cio_http_client *, const char *at, size_t length);
typedef struct cio_http_request_handler *(*cio_http_alloc_handler)(const void *config);

typedef void (*cio_http_serve_error_cb)(struct cio_http_server *server);

struct cio_http_request_handler {
	cio_http_data_cb on_url;
	cio_http_data_cb on_schema;
	cio_http_data_cb on_host;
	cio_http_data_cb on_path;
	cio_http_data_cb on_query;
	cio_http_data_cb on_fragment;
	cio_http_data_cb on_header_field;
	cio_http_data_cb on_header_value;
	cio_http_cb on_headers_complete;
	cio_http_data_cb on_body;
	cio_http_cb on_message_complete;
	void (*free)(struct cio_http_request_handler *handler);
};

struct cio_http_uri_server_location {
	/**
	 * @privatesection
	 */
	const char *path;
	cio_http_alloc_handler alloc_handler;
	struct cio_http_uri_server_location *next;
	const void *config;
};

enum cio_error cio_http_server_location_init(struct cio_http_uri_server_location *location, const char *path, const void *config, cio_http_alloc_handler handler);

struct cio_http_server {
	uint16_t port;
	struct cio_eventloop *loop;
	cio_alloc_client alloc_client;
	cio_free_client free_client;

	enum cio_error (*serve)(struct cio_http_server *server);
	enum cio_error (*register_location)(struct cio_http_server *server, struct cio_http_uri_server_location *location);

	/**
	 * @privatesection
	 */
	uint64_t read_timeout;
	cio_http_serve_error_cb error_cb;
	struct cio_server_socket server_socket;
	struct cio_http_uri_server_location *first_handler;
	size_t num_handlers;
};

enum cio_error cio_http_server_init(struct cio_http_server *server,
                                    uint16_t port,
                                    struct cio_eventloop *loop,
                                    cio_http_serve_error_cb error_cb,
                                    uint64_t read_timeout,
                                    cio_alloc_client alloc_client,
                                    cio_free_client free_client);

void cio_http_request_handler_init(struct cio_http_request_handler *handler);

#ifdef __cplusplus
}
#endif

#endif
