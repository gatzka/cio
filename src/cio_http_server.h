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
#include "cio_write_buffer.h"
#include "cio_server_socket.h"
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
	cio_http_post = HTTP_POST,
	cio_http_put = HTTP_PUT,
	cio_http_head = HTTP_HEAD,
};

enum cio_http_cb_return {
	cio_http_cb_success = 0,
	cio_http_cb_error = -1
};

struct cio_http_client {

	void (*close)(struct cio_http_client *client);
	void (*write_response)(struct cio_http_client *client, struct cio_write_buffer *wbh);
	void (*write_header)(struct cio_http_client *client, enum cio_http_status_code status);

	struct cio_buffered_stream bs;
	struct cio_read_buffer rb;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb_http_header;

	uint16_t http_major;
	uint16_t http_minor;
	enum cio_http_method http_method;

	struct cio_http_request_handler *handler;

	/**
	 * @privatesection
	 */
	struct cio_http_server *server;
	struct cio_socket socket;

	http_parser parser;
	http_parser_settings parser_settings;

	size_t buffer_size;
	uint8_t buffer[];
};

typedef enum cio_http_cb_return (*cio_http_cb) (struct cio_http_client *);
typedef enum cio_http_cb_return (*cio_http_data_cb) (struct cio_http_client *, const char *at, size_t length);

struct cio_http_request_handler {
	cio_http_cb      on_message_begin;
	cio_http_data_cb on_url;
	cio_http_data_cb on_status;
	cio_http_data_cb on_header_field;
	cio_http_data_cb on_header_value;
	cio_http_cb      on_headers_complete;
	cio_http_data_cb on_body;
	cio_http_cb      on_message_complete;
	void (*free)(struct cio_http_request_handler *handler);
};

struct cio_http_request_target {
	const char *request_target;
	struct cio_http_request_handler *(*alloc_handler)(void);
};

struct cio_http_server {
	uint16_t port;
	const struct cio_http_request_target *handler;
	size_t num_handlers;
	struct cio_eventloop *loop;
	cio_alloc_client alloc_client;
	cio_free_client free_client;

	/**
	 * @privatesection
	 */
	struct cio_server_socket server_socket;
};

//TODO: make following function a member of cio_http_server
enum cio_error cio_http_server_serve(struct cio_http_server *server);

#ifdef __cplusplus
}
#endif

#endif
