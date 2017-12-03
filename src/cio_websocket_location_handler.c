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

#include <string.h>

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

static enum cio_http_cb_return save_websocket_key(uint8_t *dest, const char *at, size_t length)
{
	static const char ws_guid[SEC_WEB_SOCKET_GUID_LENGTH] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

	if (length == SEC_WEB_SOCKET_KEY_LENGTH) {
		memcpy(dest, at, length);
		memcpy(&dest[length], ws_guid, sizeof(ws_guid));
		return cio_http_cb_success;
	} else {
		return cio_http_cb_error;
	}
}

static enum cio_http_cb_return check_websocket_version(const char *at, size_t length)
{
	static const char version[] = "13";
	if ((length == sizeof(version) - 1) && (memcmp(at, version, length) == 0)) {
		return cio_http_cb_success;
	} else {
		return cio_http_cb_error;
	}
}

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
	enum cio_http_cb_return ret;
	struct cio_websocket_location_handler *ws = container_of(client, struct cio_websocket_location_handler, http_location);

	switch (ws->current_header_field) {
	case HEADER_SEC_WEBSOCKET_KEY:
		ret = save_websocket_key(ws->sec_web_socket_key, at, length);
		break;

	case HEADER_SEC_WEBSOCKET_VERSION:
		ret = check_websocket_version(at, length);
		break;
/*
	case HEADER_SEC_WEBSOCKET_PROTOCOL:
		s->protocol_requested = true;
		check_websocket_protocol(s, at, length);
		break;

*/
	case HEADER_UNKNOWN:
	default:
		ret = cio_http_cb_success;
		break;
	}

	ws->current_header_field = HEADER_UNKNOWN;
	return ret;
}
void cio_websocket_location_handler_init(struct cio_websocket_location_handler *handler, const char *sub_protocols[], unsigned int num_sub_protocols)
{
	cio_http_location_handler_init(&handler->http_location);
	handler->current_header_field = 0;
	handler->sub_protocols = sub_protocols;
	handler->number_sub_protocols = num_sub_protocols;
	handler->http_location.on_header_field = handle_field;
	handler->http_location.on_header_value = handle_value;
}
