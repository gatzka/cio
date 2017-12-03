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

#include "cio_http_client.h"
#include "cio_http_location_handler.h"
#include "cio_string.h"
#include "cio_util.h"
#include "cio_websocket_location_handler.h"

enum header_field {
	HEADER_UNKNOWN,
	HEADER_SEC_WEBSOCKET_KEY,
	HEADER_SEC_WEBSOCKET_VERSION,
	HEADER_SEC_WEBSOCKET_PROTOCOL,
};


static enum cio_http_cb_return handle_field(struct cio_http_client *client, const char *at, size_t length)
{
	(void)client;

	static const char sec_key[] = "Sec-WebSocket-Key";
	static const char ws_version[] = "Sec-WebSocket-Version";
	static const char ws_protocol[] = "Sec-WebSocket-Protocol";

	struct cio_websocket_location_handler *ws = container_of(client, struct cio_websocket_location_handler, http_location);

	if ((sizeof(sec_key) - 1 == length) && (cio_strncasecmp(at, sec_key, length) == 0)) {
		ws->current_header_field = HEADER_SEC_WEBSOCKET_KEY;
	} else if ((sizeof(ws_version) - 1 == length) && (cio_strncasecmp(at, ws_version, length) == 0)) {
		ws->current_header_field = HEADER_SEC_WEBSOCKET_VERSION;
	} else if ((sizeof(ws_protocol) - 1 == length) && (cio_strncasecmp(at, ws_protocol, length) == 0)) {
		ws->current_header_field = HEADER_SEC_WEBSOCKET_PROTOCOL;
	}

	return cio_http_cb_success;
}

static enum cio_http_cb_return handle_value(struct cio_http_client *client, const char *at, size_t length)
{
	(void)client;
	(void)at;
	(void)length;
	return cio_http_cb_success;
}
void cio_websocket_location_handler_init(struct cio_websocket_location_handler *handler)
{
	cio_http_location_handler_init(&handler->http_location);
	handler->current_header_field = 0;
	handler->http_location.on_header_field = handle_field;
	handler->http_location.on_header_value = handle_value;
}
