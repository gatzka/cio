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

/**
 * @file
 * @brief This file contains the declarations you need to know if you
 * want to implement an http server.
 *
 * A cio_http_server gives you the ability to register multiple
 * location handlers which will be instantianted automatically
 * if an HTTP request matches a location.
 *
 * Inside a handler you can specify lots of callback functions like
 * @ref req_handler_on_header_field "on_header_field" or
 * @ref req_handler_on_body "on_body" which will be called automatically
 * when an HTTP request is processed by the http server.
 */

/**
 * @brief The cio_http_status_code enum lists all HTTP status codes that
 * can be emmited by the cio_http_server.
 */
enum cio_http_status_code {
	cio_http_status_ok = 200,                    /*!< Standard response for a successful HTTP request. */
	cio_http_status_bad_request = 400,           /*!< Request not processed due to a client error. */
	cio_http_status_not_found = 404,             /*!< The requested resource was not found. */
	cio_http_status_internal_server_error = 500, /*!< An internal server error occured. */
};

/**
 * @brief The cio_http_method enum lists all HTTP methods currently understood
 * by the HTTP parser.
 */
enum cio_http_method {
	cio_http_delete = HTTP_DELETE,           /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.5">DELETE</a> method deletes the specified resource. */
	cio_http_get = HTTP_GET,                 /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.1">GET</a> method requests a representation of the specified resource. */
	cio_http_head = HTTP_HEAD,               /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.2">HEAD</a> method asks for a response identical to that of a GET request, but without the response body. */
	cio_http_post = HTTP_POST,               /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.3">POST</a> method is used to submit an entity to the specified resource. */
	cio_http_put = HTTP_PUT,                 /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.4">PUT</a> method replaces all current representations of the target resource with the request payload. */
	cio_http_connect = HTTP_CONNECT,         /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.6">CONNECT</a> method establishes a tunnel to the server identified by the target resource. */
	cio_http_options = HTTP_OPTIONS,         /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.7">OPTIONS</a> method is used to describe the communication options for the target resource. */
	cio_http_trace = HTTP_TRACE,             /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.8">TRACE</a> method performs a message loop-back test along the path to the target resource. */
	cio_http_copy = HTTP_COPY,               /*!< WebDAV: Copy a resource from one URI to another. */
	cio_http_lock = HTTP_LOCK,               /*!< WebDAV: Put a lock on a resource. */
	cio_http_mkcol = HTTP_MKCOL,             /*!< WebDAV: Create collections (a.k.a. a directory). */
	cio_http_move = HTTP_MOVE,               /*!< WebDAV: Move a resource from one URI to another. */
	cio_http_propfind = HTTP_PROPFIND,       /*!< WebDAV: Retrieve properties, stored as XML, from a web resource. */
	cio_http_proppatch = HTTP_PROPPATCH,     /*!< WebDAV: Change and delete multiple properties on a resource in a single atomic act. */
	cio_http_search = HTTP_SEARCH,           /*!< WebDAV: Search for DAV resources based on client-supported criteria. */
	cio_http_unlock = HTTP_UNLOCK,           /*!< WebDAV: Unlock on a resource. */
	cio_http_bind = HTTP_BIND,               /*!< WebDAV: Mechanism for allowing clients to create alternative access paths to existing WebDAV resources. */
	cio_http_rebind = HTTP_REBIND,           /*!< WebDAV: Move a binding to another collection. */
	cio_http_unbind = HTTP_UNBIND,           /*!< WebDAV: Remove a binding to a resource. */
	cio_http_acl = HTTP_ACL,                 /*!< WebDAV: Modifies the access control list of a resource. */
	cio_http_report = HTTP_REPORT,           /*!< WebDAV: Obtain information about a resource. */
	cio_http_mkactivity = HTTP_MKACTIVITY,   /*!< WebDAV: Create a new activity resource. */
	cio_http_checkout = HTTP_CHECKOUT,       /*!< WebDAV: Create a new working resource from an applied version. */
	cio_http_merge = HTTP_MERGE,             /*!< WebDAV: Part of the versioning extension. */
	cio_http_msearch = HTTP_MSEARCH,         /*!< Used for upnp. */
	cio_http_notify = HTTP_NOTIFY,           /*!< Used for upnp. */
	cio_http_subscribe = HTTP_SUBSCRIBE,     /*!< Used for upnp. */
	cio_http_unsubscribe = HTTP_UNSUBSCRIBE, /*!< Used for upnp. */
	cio_http_patch = HTTP_PATCH,             /*!< The PATCH method is used to apply partial modifications to a resource. */
	cio_http_purge = HTTP_PURGE,             /*!< Used for cache invalidation. */
	cio_http_mkcalendar = HTTP_MKCALENDAR,   /*!< CalDAV: Create a calendar. */
	cio_http_link = HTTP_LINK,               /*!< Used to establish one or more relationships between an existing resource. */
	cio_http_unlink = HTTP_UNLINK            /*!< Used to remove one or more relationships between the existing resource. */
};

/**
 * @brief The cio_http_cb_return enum lists the allowed return values of user specified
 * callback functions like @ref req_handler_on_header_field "on_header_field".
 */
enum cio_http_cb_return {
	cio_http_cb_success = 0, /*!< The callback functions did not encounter any errors. */
	cio_http_cb_error = -1   /*!< The callback function encountered an error. */
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
	unsigned int parsing;

	http_parser parser;
	http_parser_settings parser_settings;

	void (*finish_func)(struct cio_http_client *client);

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
	cio_http_data_cb on_port;
	cio_http_data_cb on_path;
	cio_http_data_cb on_query;
	cio_http_data_cb on_fragment;

	/**
	 * @anchor req_handler_on_header_field
	 * @brief Called for every header field inside an http header.
	 */
	cio_http_data_cb on_header_field;

	cio_http_data_cb on_header_value;
	cio_http_cb on_headers_complete;

	/**
	 * @anchor req_handler_on_body
	 * @brief Called for processed chunks of an http body.
	 */
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
	enum cio_error (*serve)(struct cio_http_server *server);
	enum cio_error (*register_location)(struct cio_http_server *server, struct cio_http_uri_server_location *location);

	/**
	 * @privatesection
	 */
	uint16_t port;
	struct cio_eventloop *loop;
	cio_alloc_client alloc_client;
	cio_free_client free_client;

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
