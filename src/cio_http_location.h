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

#ifndef CIO_HTTP_LOCATION_H
#define CIO_HTTP_LOCATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cio_error_code.h"
#include "cio_export.h"
#include "cio_http_location_handler.h"

/**
 * @file
 * @brief This file contains the declarations for initializing an location in an HTTP server.
 *
 * A location is some kind of web space container a @ref cio_http_location_handler "location handler" is responsible for serving.
 * After initialization, a cio_http_location can be @ref cio_http_server_register "registered" to the \ref cio_http_server "HTTP server".
 */

/**
 * @brief The type of a function which allocates a cio_http_location_handler.
 *
 * @param config A configuration which is interpreted specifically in the allocated handler. See also the documentation of
 * the @ref cio_http_location_init_config "config" parameter in ::cio_http_location_init.
 *
 * @return The pointer to the allocated handler, \p NULL if the memory could not be allocated.
 */
typedef struct cio_http_location_handler *(*cio_http_alloc_handler)(const void *config);

/**
 * @brief An opaque structure encapsulating the information of an HTTP location.
 */
struct cio_http_location {
	/**
	 * @privatesection
	 */
	const char *path;
	cio_http_alloc_handler alloc_handler;
	struct cio_http_location *next;
	const void *config;
};

/**
 * @brief Initializes a cio_http_location.
 * @param location The location to be initialized.
 * @param path The path this location is responsible for.
 * @anchor cio_http_location_init_config
 * @param config A configuration which is specific for the location. Consider you want to install a file handler in two different locations,
 * for instance to /html/files/ and css/files. Both file handlers shall have different document roots (where to start looking in a file system).
 * This document root information could be passed to the handler using the \p config parameter.
 * @anchor cio_http_alloc_handler_handler
 * @param handler The allocation handler which is called an HTTP request matches the location.
 * @return ::CIO_SUCCESS if no error occured
 */
CIO_EXPORT enum cio_error cio_http_location_init(struct cio_http_location *location, const char *path, const void *config, cio_http_alloc_handler handler);

#ifdef __cplusplus
}
#endif

#endif
