/*
 * SPDX-License-Identifier: MIT
 *
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

#ifndef CIO_HTTP_CLIENT_H
#define CIO_HTTP_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cio_buffered_stream.h"
#include "cio_http_method.h"
#include "cio_http_status_code.h"
#include "cio_read_buffer.h"
#include "cio_socket.h"
#include "cio_timer.h"
#include "cio_write_buffer.h"
#include "http-parser/http_parser.h"

/**
 * @file
 * @brief The interface to an HTTP client connection.
 */

struct cio_http_client;

/**
 * @brief The type of a response callback function passed to the @ref cio_http_client_write_response "write_response" function.
 *
 * @param client The cio_http_client that wrote the response.
 * @param err An error code. If @c err != ::CIO_SUCCESS, the client will be closed automatically. So please don't
 * @ref cio_http_client_close "close" the client in the callback function. Any other resources not associated with @c client should
 * be cleaned up here.
 */
typedef void (*cio_response_written_cb)(struct cio_http_client *client, enum cio_error err);

#define CIO_HTTP_CLIENT_CONTENT_LENGTH_BUFFER_LENGTH 30
struct cio_http_client_private {
	struct cio_write_buffer wb_http_response_statusline;
	struct cio_write_buffer wb_http_content_length;
	struct cio_write_buffer wb_http_connection_header;
	struct cio_write_buffer wb_http_keepalive_header;
	struct cio_write_buffer wb_http_response_header_end;
	struct cio_timer request_timer;
	struct cio_timer response_timer;

	size_t remaining_content_length;
	bool should_keepalive;
	bool close_immediately;
	bool headers_complete;
	bool to_be_closed;
	unsigned int parsing;
	bool response_fired;

	void (*finish_func)(struct cio_http_client *client);
	char content_length_buffer[CIO_HTTP_CLIENT_CONTENT_LENGTH_BUFFER_LENGTH];
};

/**
 * @brief A cio_http_client struct represents an HTTP client connection.
 *
 * A cio_http_client is automatically created by the cio_http_server
 * after a successfully established TCP connection. The memory allocation is
 * done by the @ref cio_http_server_init_alloc_client "alloc_client" function provided during
 * @ref cio_http_server_init "initialization" of a cio_http_server.
 * The resources allocated for the HTTP client are automatically freed when the client
 * connection is @ref cio_http_client_close "closed" by using the
 * @ref cio_http_server_init_free_client "free_client" function provided during
 * @ref cio_http_server_init "initialization" of a cio_http_server.
 */
struct cio_http_client {

	/**
	 * @anchor cio_http_client_close
	 * @brief Closes the HTTP client connection.
	 *
	 * @param client The client which shall be close
	 */
	void (*close)(struct cio_http_client *client);

	/**
	 * @anchor cio_http_client_write_response
	 * @brief Writes a non-chunked response to the requesting client.
	 *
	 * This function writes a non-chunked response to the requesting client. This function takes care itself to
	 * add @c Content-Length and @c Connection header fields. Additional header fields can
	 * be added by calling @ref cio_http_client_add_response_header "add_response_header".
	 *
	 * @warning Any data belonging to response (namely the body data @p wbh_body points to and all header lines added
	 * by @ref cio_http_client_add_response_header "add_response_header") must be available and valid until the complete
	 * HTTP response was sent or the connection to the client @ref cio_http_client_close "is closed".
	 * The response was sent when the @p response_written callback function was called.
	 *
	 * @param client The client which shall get the response.
	 * @param status_code The http status code of the response.
	 * @param wbh_body The write buffer head containing the data which should be written after
	 * the HTTP response header to the client (the http body).
	 * @param response_written An optional callback that will be called whe the response was written. If @p err != ::CIO_SUCCESS,
	 * the client will be automatically closed.
	 * @return ::CIO_SUCCESS for success. If return value not ::CIO_SUCCESS, this typically means that you tried
	 * to send two responses per request.
	 */
	enum cio_error (*write_response)(struct cio_http_client *client, enum cio_http_status_code status_code, struct cio_write_buffer *wbh_body, cio_response_written_cb written_cb);

	/**
	 * @anchor cio_http_client_add_response_header
	 * @brief Queues an additional HTTP header entry.
	 *
	 * This function queues an additional HTTP header entry which will be sent when an @ref cio_http_client_write_response "HTTP response is sent".
	 * Calling this function does not cause any data sent to the client!
	 *
	 * @warning The user of this function must take care that a complete header line is given in @p wbh! Otherwise the whole header might not
	 * be valid HTTP!
	 *
	 * @warning The data @p wbh points to must be available and valid until the complete @ref cio_http_client_write_response "HTTP reponse was sent"
	 * or the connection to the client @ref cio_http_client_close "is closed". The response was sent when the @p response_written callback function
	 * of @ref cio_http_client_write_response "write_response" was called.
	 *
	 *
	 * @param client The client which shall get the HTTP header response entry.
	 * @param wbh The write buffer head pointing to the data of the header line.
	 */
	void (*add_response_header)(struct cio_http_client *client, struct cio_write_buffer *wbh);

	/**
	 * @anchor cio_http_client_bs
	 * @brief The buffered stream which is used to read data from and write data to the client.
	 *
	 */
	struct cio_buffered_stream bs;

	/**
	 * @brief The companion read buffer which is used by the @ref cio_http_client_bs "buffered stream".
	 * @anchor cio_http_client_rb
	 */
	struct cio_read_buffer rb;

	/**
	 * @brief The HTTP major version of the client request.
	 *
	 * Reading the HTTP major version is only valid after the HTTP start line is read,
	 * which means that the @ref cio_http_location_handler_on_url "on_url callback" was performed.
	 */
	uint16_t http_major;

	/**
	 * @brief The HTTP minor version of the client request.
	 *
	 * Reading the HTTP minor version is only valid after the HTTP start line is read,
	 * which means that the @ref cio_http_location_handler_on_url "on_url callback" was performed.
	 */
	uint16_t http_minor;

	/**
	 * @brief The <a href="https://tools.ietf.org/html/rfc7230#section-3.3.2">content length</a> of the client request.
	 *
	 * Reading the content length is only possible after the corresponding header fields
	 * were read by the HTTP parser. After you got the
	 * @ref cio_http_location_handler_on_headers_complete "on_headers_complete callback" you can rely
	 * on the field being set.
	 */
	size_t content_length;

	/**
	 * @brief The HTTP method (i.e. GET, POST, PUT, ...) of the HTTP client request.
	 *
	 * Reading the HTTP method is only valid after the HTTP start line is read,
	 * which means that the @ref cio_http_location_handler_on_url "on_url callback" was performed.
	 */
	enum cio_http_method http_method;

	/*! @cond PRIVATE */
	struct cio_http_location_handler *current_handler;
	struct cio_socket socket;
	struct cio_write_buffer response_wbh;
	struct cio_http_client_private http_private;
	cio_response_written_cb response_written_cb;
	bool request_complete;
	bool response_written;

	http_parser parser;
	http_parser_settings parser_settings;

	size_t buffer_size;
	uint8_t buffer[];
	/*! @endcond */
};

#ifdef __cplusplus
}
#endif

#endif
