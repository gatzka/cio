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

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cio_base64.h"
#include "cio_compiler.h"
#include "cio_http_client.h"
#include "cio_http_location_handler.h"
#include "cio_http_method.h"
#include "cio_http_status_code.h"
#include "cio_string.h"
#include "cio_util.h"
#include "cio_websocket.h"
#include "cio_websocket_location_handler.h"
#include "cio_write_buffer.h"
#include "sha1/sha1.h"

#define CIO_CRLF "\r\n"

enum header_field {
	CIO_WS_HEADER_UNKNOWN,
	CIO_WS_HEADER_SEC_WEBSOCKET_KEY,
	CIO_WS_HEADER_SEC_WEBSOCKET_VERSION,
	CIO_WS_HEADER_SEC_WEBSOCKET_PROTOCOL,
};

static enum cio_http_cb_return save_websocket_key(struct cio_websocket_location_handler *wslh, const char *at, size_t length)
{
	static const char ws_guid[SEC_WEB_SOCKET_GUID_LENGTH] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

	if (cio_likely(length == SEC_WEB_SOCKET_KEY_LENGTH)) {
		memcpy(wslh->sec_websocket_key, at, length);
		memcpy(&wslh->sec_websocket_key[length], ws_guid, sizeof(ws_guid));
		return CIO_HTTP_CB_SUCCESS;
	}

	return CIO_HTTP_CB_ERROR;
}

static enum cio_http_cb_return check_websocket_version(struct cio_websocket_location_handler *wslh, const char *at, size_t length)
{
	static const char version[2] = "13";
	if (cio_likely((length == sizeof(version)) && (memcmp(at, version, length) == 0))) {
		wslh->flags.ws_version_ok = 1;
		return CIO_HTTP_CB_SUCCESS;
	}

	return CIO_HTTP_CB_ERROR;
}

static bool find_requested_sub_protocol(struct cio_websocket_location_handler *handler, const char *name, size_t length)
{
	for (unsigned int i = 0; i < handler->number_subprotocols; i++) {
		const char *sub_protocol = handler->subprotocols[i];
		size_t name_length = strlen(sub_protocol);
		if ((name_length == length) && (memcmp(sub_protocol, name, length) == 0)) {
			handler->chosen_subprotocol = (signed int)i;
			return true;
		}
	}

	return false;
}

static void check_websocket_protocol(struct cio_websocket_location_handler *handler, const char *at, size_t length)
{
	if (handler->subprotocols == NULL) {
		return;
	}

	const char *start = at;
	while (length > 0) {
		if (!isspace(*start) && (*start != ',')) {
			const char *end = start;
			while (length > 0) {
				if (*end == ',') {
					ptrdiff_t len = end - start;
					if (find_requested_sub_protocol(handler, start, (size_t)len)) {
						return;
					}

					start = end;
					break;
				}
				end++;
				length--;
			}
			if (length == 0) {
				ptrdiff_t len = end - start;
				if (find_requested_sub_protocol(handler, start, (size_t)len)) {
					return;
				}
			}
		} else {
			start++;
			length--;
		}
	}
}

static enum cio_http_cb_return handle_field(struct cio_http_client *client, const char *at, size_t length)
{
	static const char sec_key[] = "Sec-WebSocket-Key";
	static const char ws_version[] = "Sec-WebSocket-Version";
	static const char ws_protocol[] = "Sec-WebSocket-Protocol";

	struct cio_websocket_location_handler *ws = container_of(client->handler, struct cio_websocket_location_handler, http_location);

	if ((sizeof(sec_key) - 1 == length) && (cio_strncasecmp(at, sec_key, length) == 0)) {
		ws->flags.current_header_field = CIO_WS_HEADER_SEC_WEBSOCKET_KEY;
	} else if ((sizeof(ws_version) - 1 == length) && (cio_strncasecmp(at, ws_version, length) == 0)) {
		ws->flags.current_header_field = CIO_WS_HEADER_SEC_WEBSOCKET_VERSION;
	} else if ((sizeof(ws_protocol) - 1 == length) && (cio_strncasecmp(at, ws_protocol, length) == 0)) {
		ws->flags.current_header_field = CIO_WS_HEADER_SEC_WEBSOCKET_PROTOCOL;
	}

	return CIO_HTTP_CB_SUCCESS;
}

static enum cio_http_cb_return handle_value(struct cio_http_client *client, const char *at, size_t length)
{
	enum cio_http_cb_return ret;

	struct cio_websocket_location_handler *wslh = container_of(client->handler, struct cio_websocket_location_handler, http_location);

	switch (wslh->flags.current_header_field) {
	case CIO_WS_HEADER_SEC_WEBSOCKET_KEY:
		ret = save_websocket_key(wslh, at, length);
		break;

	case CIO_WS_HEADER_SEC_WEBSOCKET_VERSION:
		ret = check_websocket_version(wslh, at, length);
		break;

	case CIO_WS_HEADER_SEC_WEBSOCKET_PROTOCOL:
		wslh->flags.subprotocol_requested = 1;
		ret = CIO_HTTP_CB_SUCCESS;
		check_websocket_protocol(wslh, at, length);
		break;

	default:
		ret = CIO_HTTP_CB_SUCCESS;
		break;
	}

	wslh->flags.current_header_field = CIO_WS_HEADER_UNKNOWN;
	return ret;
}

static bool check_http_version(const struct cio_http_client *client)
{
	if (client->http_major > 1) {
		return true;
	}

	if ((client->http_major == 1) && (client->http_minor >= 1)) {
		return true;
	}

	return false;
}

static void response_written(struct cio_buffered_stream *bs, void *handler_context, enum cio_error err)
{
	struct cio_http_client *client = (struct cio_http_client *)handler_context;

	if (cio_unlikely(err != CIO_SUCCESS)) {
		client->close(client);
		return;
	}

	struct cio_websocket_location_handler *wslh = container_of(client->handler, struct cio_websocket_location_handler, http_location);

	struct cio_websocket *ws = &wslh->websocket;
	ws->ws_private.bs = bs;
	ws->ws_private.rb = &client->rb;
	ws->ws_private.loop = client->socket.loop;

	ws->on_connect(ws);
}

static void send_upgrade_response(struct cio_http_client *client)
{
	struct SHA1Context context;
	SHA1Reset(&context);

	struct cio_websocket_location_handler *ws = container_of(client->handler, struct cio_websocket_location_handler, http_location);
	SHA1Input(&context, ws->sec_websocket_key, SEC_WEB_SOCKET_GUID_LENGTH + SEC_WEB_SOCKET_KEY_LENGTH);
	uint8_t sha1_buffer[SHA1HashSize];
	SHA1Result(&context, sha1_buffer);
	cio_b64_encode_buffer(sha1_buffer, SHA1HashSize, ws->accept_value);
	ws->accept_value[28] = '\r';
	ws->accept_value[29] = '\n';

	client->queue_header(client, CIO_HTTP_SWITCHING_PROTOCOLS);

	static const char upgrade_header[] =
	    "Upgrade: websocket" CIO_CRLF
	    "Connection: Upgrade" CIO_CRLF
	    "Sec-WebSocket-Accept: ";

	cio_write_buffer_const_element_init(&ws->wb_upgrade_header, upgrade_header, sizeof(upgrade_header) - 1);
	cio_write_buffer_const_element_init(&ws->wb_accept_value, &ws->accept_value, sizeof(ws->accept_value));
	cio_write_buffer_queue_before(&client->wbh, &client->http_private.wb_http_response_header_end, &ws->wb_upgrade_header);
	cio_write_buffer_queue_before(&client->wbh, &client->http_private.wb_http_response_header_end, &ws->wb_accept_value);

	if (ws->chosen_subprotocol != -1) {
		static const char ws_protocol[] =
		    "Sec-Websocket-Protocol: ";
		cio_write_buffer_const_element_init(&ws->wb_protocol_field, ws_protocol, sizeof(ws_protocol) - 1);
		const char *chosen_subprotocol = ws->subprotocols[ws->chosen_subprotocol];
		cio_write_buffer_const_element_init(&ws->wb_protocol_value, chosen_subprotocol, strlen(chosen_subprotocol));
		cio_write_buffer_queue_before(&client->wbh, &client->http_private.wb_http_response_header_end, &ws->wb_protocol_field);
		cio_write_buffer_queue_before(&client->wbh, &client->http_private.wb_http_response_header_end, &ws->wb_protocol_value);
		cio_write_buffer_const_element_init(&ws->wb_protocol_end, CIO_CRLF, strlen(CIO_CRLF));
		cio_write_buffer_queue_before(&client->wbh, &client->http_private.wb_http_response_header_end, &ws->wb_protocol_end);
	}

	client->flush(client, response_written);
}

static enum cio_http_cb_return handle_headers_complete(struct cio_http_client *client)
{
	if (cio_unlikely(!check_http_version(client))) {
		return CIO_HTTP_CB_ERROR;
	}

	if (cio_unlikely(client->http_method != CIO_HTTP_GET)) {
		return CIO_HTTP_CB_ERROR;
	}

	if (cio_unlikely(!client->parser.upgrade)) {
		return CIO_HTTP_CB_ERROR;
	}

	struct cio_websocket_location_handler *wslh = container_of(client->handler, struct cio_websocket_location_handler, http_location);
	if (cio_unlikely((wslh->flags.subprotocol_requested == 1) && (wslh->chosen_subprotocol == -1))) {
		return CIO_HTTP_CB_ERROR;
	}

	if (cio_unlikely(wslh->flags.ws_version_ok == 0)) {
		return CIO_HTTP_CB_ERROR;
	}

	if (cio_unlikely(wslh->sec_websocket_key[0] == 0)) {
		return CIO_HTTP_CB_ERROR;
	}

	send_upgrade_response(client);

	return CIO_HTTP_CB_SKIP_BODY;
}

static void close_server_websocket(struct cio_websocket *s)
{
	struct cio_websocket_location_handler *wslh = container_of(s, struct cio_websocket_location_handler, websocket);
	struct cio_http_client *client = wslh->http_location.client;
	client->close(client);
}

enum cio_error cio_websocket_location_handler_init(struct cio_websocket_location_handler *handler, const char *subprotocols[], unsigned int num_subprotocols, cio_websocket_on_connect on_connect)
{
	handler->flags.current_header_field = 0;
	handler->flags.ws_version_ok = 0;
	handler->flags.subprotocol_requested = 0;
	handler->chosen_subprotocol = -1;
	handler->subprotocols = subprotocols;
	handler->number_subprotocols = num_subprotocols;
	handler->sec_websocket_key[0] = 0;

	cio_http_location_handler_init(&handler->http_location);
	handler->http_location.on_header_field = handle_field;
	handler->http_location.on_header_value = handle_value;
	handler->http_location.on_headers_complete = handle_headers_complete;
	return cio_websocket_init(&handler->websocket, true, on_connect, close_server_websocket);
}
