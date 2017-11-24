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

#ifndef CIO_HTTP_METHOD_H
#define CIO_HTTP_METHOD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "http-parser/http_parser.h"
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

#ifdef __cplusplus
}
#endif

#endif
