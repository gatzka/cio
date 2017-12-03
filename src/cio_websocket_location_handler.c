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
#include <stdint.h>
#include <string.h>

#include "cio_base64.h"
#include "cio_compiler.h"
#include "cio_http_client.h"
#include "cio_http_location_handler.h"
#include "cio_string.h"
#include "cio_util.h"
#include "cio_websocket_location_handler.h"
#include "cio_write_buffer.h"
#include "sha1/sha1.h"

#define CIO_CRLF "\r\n"

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

static bool find_requested_sub_protocol(struct cio_websocket_location_handler *handler, const char *name, size_t length)
{
	for (unsigned int i = 0; i < handler->number_subprotocols; i++) {
		const char *sub_protocol = handler->subprotocols[i];
		size_t name_length = strlen(sub_protocol);
		if (name_length == length) {
			if (memcmp(sub_protocol, name, length) == 0) {
				handler->chosen_subprotocol = i;
				return true;
			}
		}
	}

	return false;
}

static void check_websocket_protocol(struct cio_websocket_location_handler *handler, const char *at, size_t length)
{
	if (handler->subprotocols != NULL) {
		const char *start = at;
		while (length > 0) {
			if (!isspace(*start) && (*start != ',')) {
				const char *end = start;
				while (length > 0) {
					if (*end == ',') {
						ptrdiff_t len = end - start;
						if (find_requested_sub_protocol(handler, start, len)) {
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
					if (find_requested_sub_protocol(handler, start, len)) {
						return;
					}
				}
			} else {
				start++;
				length--;
			}
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
		ws->flags.current_header_field = HEADER_SEC_WEBSOCKET_KEY;
	} else if ((sizeof(ws_version) - 1 == length) && (cio_strncasecmp(at, ws_version, length) == 0)) {
		ws->flags.current_header_field = HEADER_SEC_WEBSOCKET_VERSION;
	} else if ((sizeof(ws_protocol) - 1 == length) && (cio_strncasecmp(at, ws_protocol, length) == 0)) {
		ws->flags.current_header_field = HEADER_SEC_WEBSOCKET_PROTOCOL;
	}

	return cio_http_cb_success;
}

static enum cio_http_cb_return handle_value(struct cio_http_client *client, const char *at, size_t length)
{
	enum cio_http_cb_return ret;

	struct cio_websocket_location_handler *ws = container_of(client->handler, struct cio_websocket_location_handler, http_location);

	switch (ws->flags.current_header_field) {
	case HEADER_SEC_WEBSOCKET_KEY:
		ret = save_websocket_key(ws->sec_web_socket_key, at, length);
		break;

	case HEADER_SEC_WEBSOCKET_VERSION:
		ret = check_websocket_version(at, length);
		break;

	case HEADER_SEC_WEBSOCKET_PROTOCOL:
		ws->flags.subprotocol_requested = 1;
		ret = cio_http_cb_success;
		check_websocket_protocol(ws, at, length);
		break;

	case HEADER_UNKNOWN:
	default:
		ret = cio_http_cb_success;
		break;
	}

	ws->flags.current_header_field = HEADER_UNKNOWN;
	return ret;
}

static bool check_http_version(const struct cio_http_client *client)
{
	if (client->http_major > 1) {
		return true;
	}
	if ((client->http_major == 1) && (client->http_minor >= 1)) {
		return true;
	} else {
		return false;
	}
}

static void send_upgrade_response(struct cio_http_client *client)
{
	struct SHA1Context context;
	SHA1Reset(&context);

	struct cio_websocket_location_handler *ws = container_of(client->handler, struct cio_websocket_location_handler, http_location);
	SHA1Input(&context, ws->sec_web_socket_key, SEC_WEB_SOCKET_GUID_LENGTH + SEC_WEB_SOCKET_KEY_LENGTH);
	uint8_t sha1_buffer[SHA1HashSize];
	SHA1Result(&context, sha1_buffer);
	cio_b64_encode_string(sha1_buffer, SHA1HashSize, ws->accept_value);
	ws->accept_value[28] = '\r';
	ws->accept_value[29] = '\n';

	client->queue_header(client, cio_http_switching_protocols);

	static const char upgrade_header[] =
		"Upgrade: websocket" CIO_CRLF
		"Connection: Upgrade" CIO_CRLF
		"Sec-WebSocket-Accept: ";

	cio_write_buffer_element_init(&ws->wb_upgrade_header, upgrade_header, sizeof(upgrade_header) - 1);
	cio_write_buffer_element_init(&ws->wb_accept_value, &ws->accept_value, sizeof(ws->accept_value));
	cio_write_buffer_queue_before(&client->wbh, &client->wb_http_response_header_end, &ws->wb_upgrade_header);
	cio_write_buffer_queue_before(&client->wbh, &client->wb_http_response_header_end, &ws->wb_accept_value);

	if (ws->chosen_subprotocol != -1) {
		static const char ws_protocol[] =
			"Sec-Websocket-Protocol: ";
		cio_write_buffer_element_init(&ws->wb_protocol_field, ws_protocol, sizeof(ws_protocol) - 1);
		const char *chosen_subprotocol = ws->subprotocols[ws->chosen_subprotocol];
		cio_write_buffer_element_init(&ws->wb_protocol_value, chosen_subprotocol, strlen(chosen_subprotocol));
		cio_write_buffer_queue_before(&client->wbh, &client->wb_http_response_header_end, &ws->wb_protocol_field);
		cio_write_buffer_queue_before(&client->wbh, &client->wb_http_response_header_end, &ws->wb_protocol_value);
		//TODO: Add missing CRLF
	}

//TODO: flush the writebuffers.
}

static enum cio_http_cb_return handle_headers_complete(struct cio_http_client *client)
{
	if (unlikely(!check_http_version(client))) {
		return cio_http_cb_error;
	}

	if (unlikely(client->http_method != cio_http_get)) {
		return cio_http_cb_error;
	}

	if (unlikely(!client->parser.upgrade)) {
		return cio_http_cb_error;
	}

	struct cio_websocket_location_handler *ws = container_of(client->handler, struct cio_websocket_location_handler, http_location);
	if (unlikely((ws->flags.subprotocol_requested == 1) && (ws->chosen_subprotocol == -1))) {
		return cio_http_cb_error;
	}

	send_upgrade_response(client);

	return cio_http_cb_skip_body;
}

void cio_websocket_location_handler_init(struct cio_websocket_location_handler *handler, const char *subprotocols[], unsigned int num_subprotocols)
{
	cio_http_location_handler_init(&handler->http_location);
	handler->flags.current_header_field = 0;
	handler->chosen_subprotocol = -1;
	handler->flags.subprotocol_requested = 0;
	handler->subprotocols = subprotocols;
	handler->number_subprotocols = num_subprotocols;
	handler->http_location.on_header_field = handle_field;
	handler->http_location.on_header_value = handle_value;
	handler->http_location.on_headers_complete = handle_headers_complete;
}
