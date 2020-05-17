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

#ifndef CIO_HTTP_METHOD_H
#define CIO_HTTP_METHOD_H

#include "http-parser/http_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief List of all supported HTTP methods.
 */

/**
 * @brief The cio_http_method enum lists all HTTP methods currently understood
 * by the HTTP parser.
 */
enum cio_http_method {
	CIO_HTTP_DELETE = HTTP_DELETE, /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.5">DELETE</a> method deletes the specified resource. */
	CIO_HTTP_GET = HTTP_GET, /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.1">GET</a> method requests a representation of the specified resource. */
	CIO_HTTP_HEAD = HTTP_HEAD, /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.2">HEAD</a> method asks for a response identical to that of a GET request, but without the response body. */
	CIO_HTTP_POST = HTTP_POST, /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.3">POST</a> method is used to submit an entity to the specified resource. */
	CIO_HTTP_PUT = HTTP_PUT, /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.4">PUT</a> method replaces all current representations of the target resource with the request payload. */
	CIO_HTTP_CONNECT = HTTP_CONNECT, /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.6">CONNECT</a> method establishes a tunnel to the server identified by the target resource. */
	CIO_HTTP_OPTIONS = HTTP_OPTIONS, /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.7">OPTIONS</a> method is used to describe the communication options for the target resource. */
	CIO_HTTP_TRACE = HTTP_TRACE, /*!< The <a href="https://tools.ietf.org/html/rfc7231#section-4.3.8">TRACE</a> method performs a message loop-back test along the path to the target resource. */
	CIO_HTTP_COPY = HTTP_COPY, /*!< WebDAV: Copy a resource from one URI to another. */
	CIO_HTTP_LOCK = HTTP_LOCK, /*!< WebDAV: Put a lock on a resource. */
	CIO_HTTP_MKCOL = HTTP_MKCOL, /*!< WebDAV: Create collections (a.k.a. a directory). */
	CIO_HTTP_MOVE = HTTP_MOVE, /*!< WebDAV: Move a resource from one URI to another. */
	CIO_HTTP_PROPFIND = HTTP_PROPFIND, /*!< WebDAV: Retrieve properties, stored as XML, from a web resource. */
	CIO_HTTP_PROPPATCH = HTTP_PROPPATCH, /*!< WebDAV: Change and delete multiple properties on a resource in a single atomic act. */
	CIO_HTTP_SEARCH = HTTP_SEARCH, /*!< WebDAV: Search for DAV resources based on client-supported criteria. */
	CIO_HTTP_UNLOCK = HTTP_UNLOCK, /*!< WebDAV: Unlock on a resource. */
	CIO_HTTP_BIND = HTTP_BIND, /*!< WebDAV: Mechanism for allowing clients to create alternative access paths to existing WebDAV resources. */
	CIO_HTTP_REBIND = HTTP_REBIND, /*!< WebDAV: Move a binding to another collection. */
	CIO_HTTP_UNBIND = HTTP_UNBIND, /*!< WebDAV: Remove a binding to a resource. */
	CIO_HTTP_ACL = HTTP_ACL, /*!< WebDAV: Modifies the access control list of a resource. */
	CIO_HTTP_REPORT = HTTP_REPORT, /*!< WebDAV: Obtain information about a resource. */
	CIO_HTTP_MKACTIVITY = HTTP_MKACTIVITY, /*!< WebDAV: Create a new activity resource. */
	CIO_HTTP_CHECKOUT = HTTP_CHECKOUT, /*!< WebDAV: Create a new working resource from an applied version. */
	CIO_HTTP_MERGE = HTTP_MERGE, /*!< WebDAV: Part of the versioning extension. */
	CIO_HTTP_MSEARCH = HTTP_MSEARCH, /*!< Used for upnp. */
	CIO_HTTP_NOTIFY = HTTP_NOTIFY, /*!< Used for upnp. */
	CIO_HTTP_SUBSCRIBE = HTTP_SUBSCRIBE, /*!< Used for upnp. */
	CIO_HTTP_UNSUBSCRIBE = HTTP_UNSUBSCRIBE, /*!< Used for upnp. */
	CIO_HTTP_PATCH = HTTP_PATCH, /*!< The PATCH method is used to apply partial modifications to a resource. */
	CIO_HTTP_PURGE = HTTP_PURGE, /*!< Used for cache invalidation. */
	CIO_HTTP_MKCALENDAR = HTTP_MKCALENDAR, /*!< CalDAV: Create a calendar. */
	CIO_HTTP_LINK = HTTP_LINK, /*!< Used to establish one or more relationships between an existing resource. */
	CIO_HTTP_UNLINK = HTTP_UNLINK /*!< Used to remove one or more relationships between the existing resource. */
};

#ifdef __cplusplus
}
#endif

#endif
