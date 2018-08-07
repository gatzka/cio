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

struct cio_http_client_private {
	struct cio_write_buffer wb_http_response_statusline;
	struct cio_write_buffer wb_http_response_header_end;
	struct cio_timer read_timer;

	bool headers_complete;
	bool to_be_closed;
	unsigned int parsing;

	void (*finish_func)(struct cio_http_client *client);
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
	 * @brief Writes a success response (200) to the requesting client.
	 *
	 * The HTTP connection is closed after the response was written.
	 *
	 * @param client The client which shall get the response.
	 * @param wbh The write buffer head containing the data which should be written after
	 * the HTTP response header to the client.
	 */
	void (*write_response)(struct cio_http_client *client, struct cio_write_buffer *wbh);

	/**
	 * @anchor cio_http_client_write_header
	 * @brief Writes a response header to the requesting client.
	 *
	 * The HTTP connection is closed after the header was written.
	 *
	 * @param client The client which shall get the header.
	 * @param status The status code (like 404, or 400) of the response header.
	 */
	void (*write_header)(struct cio_http_client *client, enum cio_http_status_code status);

	/**
	 * @anchor cio_http_client_queue_header
	 * @brief Queues a response header for the requesting client without sending it.
	 *
	 * @param client The client which shall get the header.
	 * @param status The status code (like 404, or 400) of the response header.
	 */
	void (*queue_header)(struct cio_http_client *client, enum cio_http_status_code status);

	/**
	 * @anchor cio_http_client_flush
	 * @brief Flushes the write buffer attached of this client.
     *
	 * @param client The client which shall be flushed.
	 * @param handler The handler to be called when flusing completed.
     */
	void (*flush)(struct cio_http_client *client, cio_buffered_stream_write_handler handler);

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
	 * @brief A write buffer head to write data to an HTTP client.
	 *
	 * Typically you will not need direct access to the write buffer,
	 * use @ref cio_http_client_write_header "write_header" and
	 * @ref cio_http_client_write_response "write_response" to send "normal"
	 * HTTP responses to a client.
	 *
	 * If you have an upgraded HTTP connection (i.e. web sockets), you are encouraged
	 * to use this write buffer.
	 */
	struct cio_write_buffer wbh;

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
	uint64_t content_length;

	/**
	 * @brief The HTTP method (i.e. GET, POST, PUT, ...) of the HTTP client request.
	 *
	 * Reading the HTTP method is only valid after the HTTP start line is read,
	 * which means that the @ref cio_http_location_handler_on_url "on_url callback" was performed.
	 */
	enum cio_http_method http_method;

	struct cio_http_location_handler *handler;

	struct cio_socket socket;

	/*! @cond PRIVATE */
	struct cio_http_client_private http_private;

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
