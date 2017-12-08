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

#ifndef CIO_HTTP_LOCATION_HANDLER_H
#define CIO_HTTP_LOCATION_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "cio_http_client.h"

/**
 * @file
 * @brief Generic interface to write user specific HTTP handlers.
 *
 * You have to set the callback functions with respect to the HTTP data (header information and/or body)
 * you are interested in. If you don't set any callback, the @ref ::cio_http_server "HTTP server" will return
 * @ref ::CIO_HTTP_STATUS_INTERNAL_SERVER_ERROR "\"500 Internal server error\"" to the requesting client.
 *
 * Inside the callback functions you have access to the @ref cio_http_client "HTTP client" which performed
 * the request. You have to use the client functions @ref cio_http_client_write_response "write_response"
 * and @ref cio_http_client_write_header "write_header" to write HTTP responses (including a body)
 * or just a simple response header, respectively.
 *
 * In case of an upgraded HTTP connection, callback have access to the @ref cio_http_client_bs "buffered stream" used
 * by the @ref cio_http_client "HTTP client" and the companion @ref cio_http_client_rb "read buffer". This provides a
 * convenient way to take over a complete HTTP connection. Please note that for upgraded connections this handler is
 * responsible to @ref cio_http_client_close "close" the client connection!
 */

/**
 * @brief The cio_http_cb_return enum lists the allowed return values of user specified
 * callback functions like @ref cio_http_location_handler_on_header_field "on_header_field". If the data
 * provided via the callbacks doesn't match to the protocol the @ref ::cio_http_location_handler "HTTP handler" handles,
 * return ::CIO_HTTP_CB_ERROR, ::CIO_HTTP_CB_SUCCESS otherwise.
 *
 */
enum cio_http_cb_return {
	CIO_HTTP_CB_SKIP_BODY = 1,  /*!< The callback functions did an HTTP connection upgrade and the HTTP parser shall skip the body. */
	CIO_HTTP_CB_SUCCESS = 0, /*!< The callback functions did not encounter any errors. */
	CIO_HTTP_CB_ERROR = -1   /*!< The callback function encountered an error. */
};

/**
 * @brief The type of an HTTP callback function which will not get any data.
 *
 * @param client The client which made the HTTP request.
 */
typedef enum cio_http_cb_return (*cio_http_cb)(struct cio_http_client *client);

/**
 * @brief The type of an HTTP callback function which will get data.
 *
 * @param client The client which made the HTTP request.
 * @param at The pointer from where to read.
 * @param length The maximum number of bytes the callback function is allowed to read.
 */
typedef enum cio_http_cb_return (*cio_http_data_cb)(struct cio_http_client *client, const char *at, size_t length);

struct cio_http_location_handler {
	/**
	 * @anchor cio_http_location_handler_on_url
	 * @brief Called after the complete request URL was parsed.
	 */
	cio_http_data_cb on_url;

	/**
	 * @anchor cio_http_location_handler_on_schema
	 * @brief Called with the schema part of the URL.
	 */
	cio_http_data_cb on_schema;

	/**
	 * @anchor cio_http_location_handler_on_host
	 * @brief Called with the host part of the URL.
	 */
	cio_http_data_cb on_host;

	/**
	 * @anchor cio_http_location_handler_on_port
	 * @brief Called with the port part of the URL.
	 */
	cio_http_data_cb on_port;

	/**
	 * @anchor cio_http_location_handler_on_path
	 * @brief Called with the path part of the URL.
	 */
	cio_http_data_cb on_path;

	/**
	 * @anchor cio_http_location_handler_on_query
	 * @brief Called with the query part of the URL.
	 */
	cio_http_data_cb on_query;

	/**
	 * @anchor cio_http_location_handler_on_fragment
	 * @brief Called with the fragment part of the URL.
	 */
	cio_http_data_cb on_fragment;

	/**
	 * @anchor cio_http_location_handler_on_header_field
	 * @brief Called for every header field inside an HTTP header.
	 */
	cio_http_data_cb on_header_field;

	/**
	 * @anchor cio_http_location_handler_on_header_value
	 * @brief Called for every header value inside an HTTP header.
	 */
	cio_http_data_cb on_header_value;

	/**
	 * @anchor cio_http_location_handler_on_headers_complete
	 * @brief Called if the HTTP header was parsed completely.
	 */
	cio_http_cb on_headers_complete;

	/**
	 * @anchor cio_http_location_handler_on_body
	 * @brief Called for processed chunks of an HTTP body.
	 */
	cio_http_data_cb on_body;

	/**
	 * @anchor cio_http_location_handler_on_message_complete
	 * @brief Called if the HTTP message was parsed completely.
	 */
	cio_http_cb on_message_complete;

	/**
	 * @anchor cio_http_location_handler_free
	 * @brief Frees the resources @ref cio_http_alloc_handler_handler "allocated" for this handler.
	 *
	 * This callback function is called automatically when an HTTP client
	 * connection is @ref cio_http_client_close "closed".
	 */
	void (*free)(struct cio_http_location_handler *handler);

	/**
	 * @privatesection
	 */
	struct cio_http_client *client;
};

/**
 * @brief Initializes a location handler with default values.
 *
 * After the initialization, you are allowed to set any member of cio_http_location_handler.
 * @param handler The handler to be initialized.
 */
void cio_http_location_handler_init(struct cio_http_location_handler *handler);

#ifdef __cplusplus
}
#endif

#endif
