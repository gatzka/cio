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

#ifndef CIO_HTTP_REQUEST_HANDLER_H
#define CIO_HTTP_REQUEST_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "cio_http_client.h"

/**
 * @brief The cio_http_cb_return enum lists the allowed return values of user specified
 * callback functions like @ref req_handler_on_header_field "on_header_field".
 */
enum cio_http_cb_return {
	cio_http_cb_success = 0, /*!< The callback functions did not encounter any errors. */
	cio_http_cb_error = -1   /*!< The callback function encountered an error. */
};


typedef enum cio_http_cb_return (*cio_http_cb)(struct cio_http_client *);
typedef enum cio_http_cb_return (*cio_http_data_cb)(struct cio_http_client *, const char *at, size_t length);

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

void cio_http_request_handler_init(struct cio_http_request_handler *handler);

#ifdef __cplusplus
}
#endif

#endif
