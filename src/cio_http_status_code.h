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

#ifndef CIO_HTTP_STATUS_CODE_H
#define CIO_HTTP_STATUS_CODE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief List the currently supported <a href="https://tools.ietf.org/html/rfc7231#section-6">HTTP status codes</a>.
 */

/**
 * @brief The cio_http_status_code enum lists all HTTP status codes that
 * can be emmited by the cio_http_server.
 */
enum cio_http_status_code {
	cio_http_switching_protocols = 101,          /*!< The requester has asked the server to switch protocols and the server has agreed to do so. */
	cio_http_status_ok = 200,                    /*!< Standard response for a successful HTTP request. */
	cio_http_status_bad_request = 400,           /*!< Request not processed due to a client error. */
	cio_http_status_not_found = 404,             /*!< The requested resource was not found. */
	cio_http_status_internal_server_error = 500, /*!< An internal server error occured. */
};

#ifdef __cplusplus
}
#endif

#endif
