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

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cio/base64.h"
#include "cio/compiler.h"
#include "cio/eventloop.h"
#include "cio/http_client.h"
#include "cio/http_location_handler.h"
#include "cio/http_method.h"
#include "cio/http_server.h"
#include "cio/http_status_code.h"
#include "cio/sha1/sha1.h"
#include "cio/string.h"
#include "cio/timer.h"
#include "cio/util.h"
#include "cio/websocket.h"
#include "cio/websocket_location_handler.h"
#include "cio/write_buffer.h"

#define CIO_CRLF "\r\n"

enum header_field {
	CIO_WS_HEADER_UNKNOWN,
	CIO_WS_HEADER_SEC_WEBSOCKET_KEY,
	CIO_WS_HEADER_SEC_WEBSOCKET_VERSION,
	CIO_WS_HEADER_SEC_WEBSOCKET_PROTOCOL,
};

enum protocol_find_ret {
	WS_PROTOCOL_FOUND,
	WS_PROTOCOL_NOT_FOUND,
	WS_PROTOCOL_ERROR,
};

static enum cio_http_cb_return save_websocket_key(struct cio_websocket_location_handler *wslh, const char *at, size_t length)
{
	static const char WS_GUID[CIO_SEC_WEB_SOCKET_GUID_LENGTH] = {'2', '5', '8', 'E', 'A', 'F', 'A', '5', '-',
	                                                             'E', '9', '1', '4', '-', '4', '7', 'D', 'A', '-',
	                                                             '9', '5', 'C', 'A', '-',
	                                                             'C', '5', 'A', 'B', '0', 'D', 'C', '8', '5', 'B', '1', '1'};

	if (cio_likely(length == CIO_SEC_WEB_SOCKET_KEY_LENGTH)) {
		memcpy(wslh->sec_websocket_key, at, length);
		memcpy(&wslh->sec_websocket_key[length], WS_GUID, sizeof(WS_GUID));
		return CIO_HTTP_CB_SUCCESS;
	}

	return CIO_HTTP_CB_ERROR;
}

static enum cio_http_cb_return check_websocket_version(struct cio_websocket_location_handler *wslh, const char *at, size_t length)
{
	static const char VERSION[2] = {'1', '3'};
	if (cio_likely((length == sizeof(VERSION)) && (memcmp(at, VERSION, length) == 0))) {
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

static const char *find_delimiter(const char *str, size_t len)
{
	for (const char *from = str; from != str + len; from++) {
		if (*from == ',') {
			return from;
		}
	}

	return NULL;
}

static bool is_white_space(char character)
{
	if ((character == ' ') || (character == '\t')) {
		return true;
	}

	return false;
}

static const char *strip_leading_whitespace(const char *str, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (!is_white_space(str[i])) {
			return &str[i];
		}
	}

	return NULL;
}

static size_t strip_trailing_whitespace(const char *str, size_t len)
{
	while (is_white_space(str[len - 1])) {
		len--;
	}

	return len;
}

static bool is_visible_character(char character)
{
	return (character >= '!') && (character <= '~');
}

static bool is_delimiter(char character)
{
	if ((character == '"') ||
	    (character == '(') ||
	    (character == ')') ||
	    (character == '/') ||
	    (character == ':') ||
	    (character == ';') ||
	    (character == '<') ||
	    (character == '=') ||
	    (character == '>') ||
	    (character == '?') ||
	    (character == '@') ||
	    (character == '[') ||
	    (character == '\\') ||
	    (character == ']') ||
	    (character == '{') ||
	    (character == '}')) {
		return true;
	}

	return false;
}

static bool has_illegal_characters(const char *str, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (!is_visible_character(str[i])) {
			return true;
		}

		if (is_delimiter(str[i])) {
			return true;
		}
	}

	return false;
}

static enum protocol_find_ret handle_word(struct cio_websocket_location_handler *handler, const char *str, size_t len)
{
	const char *start = strip_leading_whitespace(str, len);
	if (cio_unlikely(start == NULL)) {
		return WS_PROTOCOL_ERROR;
	}

	len -= (size_t)(ptrdiff_t)(start - str);
	len = strip_trailing_whitespace(start, len);

	if (cio_unlikely(has_illegal_characters(start, len))) {
		return WS_PROTOCOL_ERROR;
	}

	if (find_requested_sub_protocol(handler, start, len)) {
		return WS_PROTOCOL_FOUND;
	}

	return WS_PROTOCOL_NOT_FOUND;
}

static enum cio_http_cb_return check_websocket_protocol(struct cio_websocket_location_handler *handler, const char *at, size_t length)
{
	if (handler->subprotocols == NULL) {
		return CIO_HTTP_CB_SUCCESS;
	}

	const char *start = at;
	const char *end = NULL;
	do {
		end = find_delimiter(start, length);
		size_t word_len = 0;
		if (end != NULL) {
			word_len = (size_t)(ptrdiff_t)(end - start);
		} else {
			word_len = length;
		}

		enum protocol_find_ret ret = handle_word(handler, start, word_len);
		if (ret == WS_PROTOCOL_FOUND) {
			return CIO_HTTP_CB_SUCCESS;
		}

		if (ret == WS_PROTOCOL_ERROR) {
			return CIO_HTTP_CB_ERROR;
		}

		start = start + 1 + word_len;
		length = length - 1 - word_len;
	} while (end != NULL);

	return CIO_HTTP_CB_ERROR;
}

static enum cio_http_cb_return handle_field_name(struct cio_http_client *client, const char *at, size_t length)
{
	static const char SEC_KEY[] = "Sec-WebSocket-Key";
	static const char WS_VERSION[] = "Sec-WebSocket-Version";
	static const char WS_PROTOCOL[] = "Sec-WebSocket-Protocol";

	struct cio_websocket_location_handler *websocket = cio_container_of(client->current_handler, struct cio_websocket_location_handler, http_location);

	if ((sizeof(SEC_KEY) - 1 == length) && (cio_strncasecmp(at, SEC_KEY, length) == 0)) {
		websocket->flags.current_header_field = CIO_WS_HEADER_SEC_WEBSOCKET_KEY;
	} else if ((sizeof(WS_VERSION) - 1 == length) && (cio_strncasecmp(at, WS_VERSION, length) == 0)) {
		websocket->flags.current_header_field = CIO_WS_HEADER_SEC_WEBSOCKET_VERSION;
	} else if ((sizeof(WS_PROTOCOL) - 1 == length) && (cio_strncasecmp(at, WS_PROTOCOL, length) == 0)) {
		websocket->flags.current_header_field = CIO_WS_HEADER_SEC_WEBSOCKET_PROTOCOL;
	}

	return CIO_HTTP_CB_SUCCESS;
}

static enum cio_http_cb_return handle_field_value(struct cio_http_client *client, const char *at, size_t length)
{
	enum cio_http_cb_return ret = CIO_HTTP_CB_SUCCESS;

	struct cio_websocket_location_handler *wslh = cio_container_of(client->current_handler, struct cio_websocket_location_handler, http_location);

	uint_fast8_t header_field = (uint_fast8_t)wslh->flags.current_header_field;

	switch (header_field) {
	case CIO_WS_HEADER_SEC_WEBSOCKET_KEY:
		ret = save_websocket_key(wslh, at, length);
		break;

	case CIO_WS_HEADER_SEC_WEBSOCKET_VERSION:
		ret = check_websocket_version(wslh, at, length);
		break;

	case CIO_WS_HEADER_SEC_WEBSOCKET_PROTOCOL:
		wslh->flags.subprotocol_requested = 1;
		ret = check_websocket_protocol(wslh, at, length);
		break;

	default:
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

static void response_written(struct cio_http_client *client, enum cio_error err)
{
	struct cio_websocket_location_handler *wslh = cio_container_of(client->current_handler, struct cio_websocket_location_handler, http_location);
	struct cio_websocket *websocket = &wslh->websocket;

	if (cio_unlikely(err != CIO_SUCCESS)) {
		struct cio_http_server *server = cio_http_client_get_server(client);
		if (server->on_error != NULL) {
			server->on_error(server, "error while writing http upgrade response");
		}

		return;
	}

	websocket->ws_private.http_client = client;
	websocket->on_connect(websocket);
}

static enum cio_error send_upgrade_response(struct cio_http_client *client)
{
	struct sha1_context context;
	sha1_reset(&context);

	struct cio_websocket_location_handler *websocket = cio_container_of(client->current_handler, struct cio_websocket_location_handler, http_location);
	sha1_input(&context, websocket->sec_websocket_key, CIO_SEC_WEB_SOCKET_GUID_LENGTH + CIO_SEC_WEB_SOCKET_KEY_LENGTH);
	uint8_t sha1_buffer[SHA1_HASH_SIZE];
	sha1_result(&context, sha1_buffer);
	cio_b64_encode_buffer(sha1_buffer, SHA1_HASH_SIZE, websocket->accept_value);
	websocket->accept_value[CIO_SEC_WEBSOCKET_ACCEPT_LENGTH - 2] = '\r';
	websocket->accept_value[CIO_SEC_WEBSOCKET_ACCEPT_LENGTH - 1] = '\n';

	static const char UPGRADE_HEADER[] =
	    "Upgrade: websocket" CIO_CRLF
	    "Sec-WebSocket-Accept: ";

	cio_write_buffer_const_element_init(&websocket->wb_upgrade_header, UPGRADE_HEADER, sizeof(UPGRADE_HEADER) - 1);
	client->add_response_header(client, &websocket->wb_upgrade_header);
	cio_write_buffer_const_element_init(&websocket->wb_accept_value, &websocket->accept_value, sizeof(websocket->accept_value));
	client->add_response_header(client, &websocket->wb_accept_value);

	if (websocket->chosen_subprotocol != -1) {
		static const char WS_PROTOCOL[] =
		    "Sec-Websocket-Protocol: ";
		cio_write_buffer_const_element_init(&websocket->wb_protocol_field, WS_PROTOCOL, sizeof(WS_PROTOCOL) - 1);
		const char *chosen_subprotocol = websocket->subprotocols[websocket->chosen_subprotocol];
		cio_write_buffer_const_element_init(&websocket->wb_protocol_value, chosen_subprotocol, strlen(chosen_subprotocol));
		cio_write_buffer_const_element_init(&websocket->wb_protocol_end, CIO_CRLF, strlen(CIO_CRLF));
		client->add_response_header(client, &websocket->wb_protocol_field);
		client->add_response_header(client, &websocket->wb_protocol_value);
		client->add_response_header(client, &websocket->wb_protocol_end);
	}

	return client->write_response(client, CIO_HTTP_STATUS_SWITCHING_PROTOCOLS, NULL, response_written);
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

	const struct cio_websocket_location_handler *wslh = cio_const_container_of(client->current_handler, struct cio_websocket_location_handler, http_location);
	if (cio_unlikely((wslh->flags.subprotocol_requested == 1) && (wslh->chosen_subprotocol == -1))) {
		return CIO_HTTP_CB_ERROR;
	}

	if (cio_unlikely(wslh->flags.ws_version_ok == 0)) {
		return CIO_HTTP_CB_ERROR;
	}

	if (cio_unlikely(wslh->sec_websocket_key[0] == 0)) {
		return CIO_HTTP_CB_ERROR;
	}

	enum cio_error err = send_upgrade_response(client);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return CIO_HTTP_CB_ERROR;
	}

	return CIO_HTTP_CB_SKIP_BODY;
}

static void close_server_websocket(struct cio_websocket *websocket)
{
	websocket->ws_private.http_client->close(websocket->ws_private.http_client);
}

static void free_resources(struct cio_http_location_handler *handler)
{
	struct cio_websocket_location_handler *wslh = cio_container_of(handler, struct cio_websocket_location_handler, http_location);
	wslh->location_handler_free(wslh);
}

enum cio_error cio_websocket_location_handler_init(struct cio_websocket_location_handler *handler,
                                                   const char *subprotocols[],
                                                   size_t num_subprotocols,
                                                   cio_websocket_on_connect_t on_connect,
                                                   void (*location_handler_free)(struct cio_websocket_location_handler *))
{
	if (cio_unlikely((handler == NULL) || (location_handler_free == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	handler->flags.current_header_field = CIO_WS_HEADER_UNKNOWN;
	handler->flags.ws_version_ok = 0;
	handler->flags.subprotocol_requested = 0;
	handler->chosen_subprotocol = -1;
	handler->subprotocols = subprotocols;
	handler->number_subprotocols = num_subprotocols;
	handler->sec_websocket_key[0] = 0;
	handler->location_handler_free = location_handler_free;

	cio_http_location_handler_init(&handler->http_location);
	handler->http_location.on_header_field_name = handle_field_name;
	handler->http_location.on_header_field_value = handle_field_value;
	handler->http_location.on_headers_complete = handle_headers_complete;
	handler->http_location.free = free_resources;

	enum cio_error err = cio_websocket_server_init(&handler->websocket, on_connect, close_server_websocket);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}
