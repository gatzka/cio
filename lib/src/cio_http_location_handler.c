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

#include <stdbool.h>

#include "cio/cio_http_location_handler.h"

void cio_http_location_handler_init(struct cio_http_location_handler *handler)
{
	handler->on_url = NULL;
	handler->on_port = NULL;
	handler->on_schema = NULL;
	handler->on_host = NULL;
	handler->on_path = NULL;
	handler->on_query = NULL;
	handler->on_fragment = NULL;
	handler->on_header_field_name = NULL;
	handler->on_header_field_value = NULL;
	handler->on_headers_complete = NULL;
	handler->on_body = NULL;
	handler->on_message_complete = NULL;
	handler->free = NULL;
}

bool cio_http_location_handler_no_callbacks(const struct cio_http_location_handler *handler)
{
	if (handler->on_url ||
	    handler->on_headers_complete ||
	    handler->on_message_complete ||
	    handler->on_header_field_name ||
	    handler->on_header_field_value ||
	    handler->on_body ||
	    handler->on_port ||
	    handler->on_host ||
	    handler->on_path ||
	    handler->on_query ||
	    handler->on_schema ||
	    handler->on_fragment) {
		return false;
	}

	return true;
}
