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

#ifndef CIO_WEBSOCKET_LOCATION_HANDLER_H
#define CIO_WEBSOCKET_LOCATION_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "cio_http_location_handler.h"

#define SEC_WEB_SOCKET_KEY_LENGTH 24
#define SEC_WEB_SOCKET_GUID_LENGTH 36

struct cio_websocket_location_handler {

	/**
	 * @privatesection
	 */
	struct cio_http_location_handler http_location;

	unsigned int current_header_field;
	uint8_t sec_web_socket_key[SEC_WEB_SOCKET_KEY_LENGTH + SEC_WEB_SOCKET_GUID_LENGTH];

	const char **sub_protocols;
	unsigned int number_sub_protocols;

};

void cio_websocket_location_handler_init(struct cio_websocket_location_handler *handler, const char *sub_protocols[], unsigned int num_sub_protocols);


#ifdef __cplusplus
}
#endif

#endif
