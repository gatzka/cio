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
 * @brief A cio_http_client struct represents an HTTP client connection.
 *
 * A cio_http_client is automatically created by the cio_http_server
 * after a successfully established TCP connection. The memory allocation is
 * done by the @ref cio_http_server_init_alloc_client "alloc_client" function provided during
 * @ref cio_http_server_init "initialization" of a cio_http_server.
 */
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

	struct cio_http_location_handler *handler;

	/**
	 * @privatesection
	 */
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

#ifdef __cplusplus
}
#endif

#endif
