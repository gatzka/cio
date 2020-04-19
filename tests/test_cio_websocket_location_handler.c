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

#include <stdio.h>
#include <stdlib.h>

#include "cio_buffered_stream.h"
#include "cio_http_server.h"
#include "cio_util.h"
#include "cio_websocket_location_handler.h"
#include "cio_websocket_masking.h"

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

FAKE_VOID_FUNC(cio_http_location_handler_init, struct cio_http_location_handler *)
FAKE_VALUE_FUNC(enum cio_error, cio_websocket_init, struct cio_websocket *, bool, cio_websocket_on_connect, cio_websocket_close_hook)
FAKE_VOID_FUNC(fake_handler_free, struct cio_websocket_location_handler *)
FAKE_VOID_FUNC(fake_add_response_header, struct cio_http_client *, struct cio_write_buffer *)
FAKE_VALUE_FUNC(enum cio_error, fake_write_response, struct cio_http_client *, enum cio_http_status_code, struct cio_write_buffer *, cio_response_written_cb)
FAKE_VOID_FUNC(on_connect, struct cio_websocket *)
FAKE_VOID_FUNC(on_error, struct cio_http_server *, const char *)
FAKE_VOID_FUNC(client_close, struct cio_http_client *)

static enum cio_error write_response_call_callback(struct cio_http_client *client, enum cio_http_status_code status, struct cio_write_buffer *buf, cio_response_written_cb response_callback)
{
	(void)status;
	(void)buf;

	response_callback(client, CIO_SUCCESS);
	return CIO_SUCCESS;
}

static enum cio_error write_response_call_callback_with_error(struct cio_http_client *client, enum cio_http_status_code status, struct cio_write_buffer *buf, cio_response_written_cb response_callback)
{
	(void)status;
	(void)buf;

	response_callback(client, CIO_NO_MEMORY);
	return CIO_SUCCESS;
}

static enum cio_error websocket_init_save_params(struct cio_websocket *ws, bool is_server, cio_websocket_on_connect on_connect_cb, cio_websocket_close_hook close_hook)
{
	ws->on_connect = on_connect_cb;
	ws->ws_private.close_hook = close_hook;
	ws->ws_private.ws_flags.is_server = is_server;
	return CIO_SUCCESS;
}

void setUp(void)
{
	FFF_RESET_HISTORY()

	RESET_FAKE(cio_http_location_handler_init)
	RESET_FAKE(cio_websocket_init)
	RESET_FAKE(fake_handler_free)
	RESET_FAKE(fake_add_response_header);
	RESET_FAKE(fake_write_response);
	RESET_FAKE(on_connect);
	RESET_FAKE(on_error);
	RESET_FAKE(client_close);

	cio_websocket_init_fake.custom_fake = websocket_init_save_params;
	fake_write_response_fake.custom_fake = write_response_call_callback;
}

void tearDown(void)
{
}

static void test_ws_location_init_ok(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, NULL, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");
}

static void test_ws_location_ws_init_fails(void)
{
	struct cio_websocket_location_handler handler;
	cio_websocket_init_fake.custom_fake = NULL;
	cio_websocket_init_fake.return_val = CIO_INVALID_ARGUMENT;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, NULL, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "web socket handler initialization didn't fail!");
}

static void test_ws_location_init_fails(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(NULL, NULL, 0, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "web socket handler initialization did not failed if no handler is provided!");

	err = cio_websocket_location_handler_init(&handler, NULL, 0, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "web socket handler initialization did not failed if no location free function is provided!");
}

static void test_free_resources(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, NULL, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

	handler.http_location.free(&handler.http_location);
	TEST_ASSERT_EQUAL_MESSAGE(1, fake_handler_free_fake.call_count, "free_resources was not called when http_location is freed!");
}

static void test_ws_location_http_versions(void)
{
	struct upgrade_test {
		uint16_t major;
		uint16_t minor;
		enum cio_http_cb_return expected_ret_val;
	};

	struct upgrade_test tests[] = {
	    {.major = 1, .minor = 1, .expected_ret_val = CIO_HTTP_CB_SKIP_BODY},
	    {.major = 2, .minor = 0, .expected_ret_val = CIO_HTTP_CB_SKIP_BODY},
	    {.major = 1, .minor = 0, .expected_ret_val = CIO_HTTP_CB_ERROR},
	    {.major = 0, .minor = 9, .expected_ret_val = CIO_HTTP_CB_ERROR},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct upgrade_test test = tests[i];

		struct cio_websocket_location_handler handler;
		enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

		struct cio_http_client client;

		handler.websocket.ws_private.http_client = &client;
		handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
		handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
		handler.websocket.ws_private.http_client->write_response = fake_write_response;

		handler.websocket.ws_private.http_client->parser.upgrade = 1;
		handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
		handler.websocket.ws_private.http_client->http_major = test.major;
		handler.websocket.ws_private.http_client->http_minor = test.minor;

		static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
		static const char sec_ws_version_value[] = "13";
		enum cio_http_cb_return cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

		static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
		static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
		cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

		cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
		TEST_ASSERT_EQUAL_MESSAGE(test.expected_ret_val, cb_ret, "on_header_complete returned wrong value");
		if (test.expected_ret_val == CIO_HTTP_CB_SKIP_BODY) {
			TEST_ASSERT_EQUAL_MESSAGE(1, fake_write_response_fake.call_count, "write_response was not called");
			TEST_ASSERT_EQUAL_MESSAGE(1, on_connect_fake.call_count, "websocket on_connect was not called");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_STATUS_SWITCHING_PROTOCOLS, fake_write_response_fake.arg1_val, "write_response was not called with CIO_HTTP_STATUS_SWITCHING_PROTOCOLS");
		}

		setUp();
	}
}

static void test_ws_location_wrong_http_method(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

	struct cio_http_client client;

	handler.websocket.ws_private.http_client = &client;
	handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
	handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
	handler.websocket.ws_private.http_client->write_response = fake_write_response;

	handler.websocket.ws_private.http_client->parser.upgrade = 1;
	handler.websocket.ws_private.http_client->http_method = CIO_HTTP_POST;
	handler.websocket.ws_private.http_client->http_major = 1;
	handler.websocket.ws_private.http_client->http_minor = 1;

	static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
	static const char sec_ws_version_value[] = "13";
	enum cio_http_cb_return cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
	cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

	static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
	static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
	cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
	cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

	cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_ERROR, cb_ret, "on_header_complete returned wrong value");
}

static void test_ws_location_no_http_upgrade(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

	struct cio_http_client client;

	handler.websocket.ws_private.http_client = &client;
	handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
	handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
	handler.websocket.ws_private.http_client->write_response = fake_write_response;

	handler.websocket.ws_private.http_client->parser.upgrade = 0;
	handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
	handler.websocket.ws_private.http_client->http_major = 1;
	handler.websocket.ws_private.http_client->http_minor = 1;

	static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
	static const char sec_ws_version_value[] = "13";
	enum cio_http_cb_return cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
	cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

	static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
	static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
	cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
	cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

	cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_ERROR, cb_ret, "on_header_complete returned wrong value");
}

static void test_ws_location_wrong_http_headers(void)
{
	struct upgrade_test {
		const char *sec_key_field;
		const char *sec_key_value;
		const char *version_field;
		const char *version_value;
		const char *protocol_field;
		const char *protocol_value;
		enum cio_http_cb_return on_header_value_retval_version;
		enum cio_http_cb_return on_header_value_retval_key;
		enum cio_http_cb_return on_headers_retval;
	};

	struct upgrade_test tests[] = {
	    {.sec_key_field = "Sec-WebSocket-Key",
	     .sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
	     .on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
	     .version_field = "Sec-WebSocket-Version",
	     .version_value = "13",
	     .on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
	     .protocol_field = "Sec-WebSocket-Protocol",
	     .protocol_value = "jet",
	     .on_headers_retval = CIO_HTTP_CB_SKIP_BODY},
	    {.sec_key_field = "Sic-WebSocket-Key",
	     .sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
	     .on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
	     .version_field = "Sec-WebSocket-Version",
	     .version_value = "13",
	     .on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
	     .protocol_field = "Sec-WebSocket-Protocol",
	     .protocol_value = "jet",
	     .on_headers_retval = CIO_HTTP_CB_ERROR},
	    {.sec_key_field = "Sec-WebSocket-Key",
	     .sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
	     .on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
	     .version_field = "Sec-WebSocket-Wersion",
	     .version_value = "13",
	     .on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
	     .protocol_field = "Sec-WebSocket-Protocol",
	     .protocol_value = "jet",
	     .on_headers_retval = CIO_HTTP_CB_ERROR},
	    {.sec_key_field = "Sec-WebSocket-Key",
	     .sec_key_value = "lIHNhbXBsZSBub25jZQ==",
	     .on_header_value_retval_key = CIO_HTTP_CB_ERROR,
	     .version_field = "Sec-WebSocket-Version",
	     .version_value = "13",
	     .on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
	     .protocol_field = "Sec-WebSocket-Protocol",
	     .protocol_value = "jet",
	     .on_headers_retval = CIO_HTTP_CB_ERROR},
	    {.sec_key_field = "Sec-WebSocket-Key",
	     .sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
	     .on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
	     .version_field = "Sec-WebSocket-Version",
	     .version_value = "12",
	     .on_header_value_retval_version = CIO_HTTP_CB_ERROR,
	     .protocol_field = "Sec-WebSocket-Protocol",
	     .protocol_value = "jet",
	     .on_headers_retval = CIO_HTTP_CB_ERROR},
	    {.sec_key_field = "Sec-WebSocket-Key",
	     .sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
	     .on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
	     .version_field = "Sec-WebSocket-Version",
	     .version_value = "2",
	     .on_header_value_retval_version = CIO_HTTP_CB_ERROR,
	     .protocol_field = "Sec-WebSocket-Protocol",
	     .protocol_value = "jet",
	     .on_headers_retval = CIO_HTTP_CB_ERROR},
	    {.sec_key_field = "Sec-WebSocket-Key",
	     .sec_key_value = "dGhlIHNhbXBsZSBub25jZQ==",
	     .on_header_value_retval_key = CIO_HTTP_CB_SUCCESS,
	     .version_field = "Sec-WebSocket-Version",
	     .version_value = "13",
	     .on_header_value_retval_version = CIO_HTTP_CB_SUCCESS,
	     .protocol_field = "Sec-WebSocket-Portocol",
	     .protocol_value = "jet",
	     .on_headers_retval = CIO_HTTP_CB_SKIP_BODY}};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct upgrade_test test = tests[i];

		const char *sub_protocols[] = {"echo", "jet"};
		struct cio_websocket_location_handler handler;
		enum cio_error err = cio_websocket_location_handler_init(&handler, sub_protocols, ARRAY_SIZE(sub_protocols), on_connect, fake_handler_free);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

		struct cio_http_client client;

		handler.websocket.ws_private.http_client = &client;
		handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
		handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
		handler.websocket.ws_private.http_client->write_response = fake_write_response;
		handler.websocket.ws_private.http_client->close = client_close;

		handler.websocket.ws_private.http_client->parser.upgrade = 1;
		handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
		handler.websocket.ws_private.http_client->http_major = 1;
		handler.websocket.ws_private.http_client->http_minor = 1;

		enum cio_http_cb_return cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, test.version_field, strlen(test.version_field));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, test.version_value, strlen(test.version_value));
		TEST_ASSERT_EQUAL_MESSAGE(test.on_header_value_retval_version, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

		cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, test.sec_key_field, strlen(test.sec_key_field));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, test.sec_key_value, strlen(test.sec_key_value));
		TEST_ASSERT_EQUAL_MESSAGE(test.on_header_value_retval_key, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

		cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, test.protocol_field, strlen(test.protocol_field));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for protocol_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, test.protocol_value, strlen(test.protocol_value));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for protocol_value");

		cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
		TEST_ASSERT_EQUAL_MESSAGE(test.on_headers_retval, cb_ret, "on_header_complete returned wrong value");
		if (test.on_headers_retval == CIO_HTTP_CB_SKIP_BODY) {
			TEST_ASSERT_EQUAL_MESSAGE(1, fake_write_response_fake.call_count, "write_response was not called");
			TEST_ASSERT_EQUAL_MESSAGE(1, on_connect_fake.call_count, "websocket on_connect was not called");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_STATUS_SWITCHING_PROTOCOLS, fake_write_response_fake.arg1_val, "write_response was not called with CIO_HTTP_STATUS_SWITCHING_PROTOCOLS");

			handler.websocket.ws_private.close_hook(&handler.websocket);
		}

		setUp();
	}
}

static void test_ws_location_send_response_fails(void)
{
	struct cio_websocket_location_handler handler;
	enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

	struct cio_http_client client;
	fake_write_response_fake.custom_fake = NULL;
	fake_write_response_fake.return_val = CIO_INVALID_ARGUMENT;

	handler.websocket.ws_private.http_client = &client;
	handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
	handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
	handler.websocket.ws_private.http_client->write_response = fake_write_response;

	handler.websocket.ws_private.http_client->parser.upgrade = 1;
	handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
	handler.websocket.ws_private.http_client->http_major = 1;
	handler.websocket.ws_private.http_client->http_minor = 1;

	static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
	static const char sec_ws_version_value[] = "13";
	enum cio_http_cb_return cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
	cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

	static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
	static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
	cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
	cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

	cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_ERROR, cb_ret, "on_header_complete returned wrong value");
}

static void test_ws_location_response_written_fails(void)
{
	struct test {
		cio_http_serve_on_error on_error;
	};

	struct test tests[] = {
	    {.on_error = NULL},
	    {.on_error = on_error},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct test test = tests[i];

		struct cio_websocket_location_handler handler;
		enum cio_error err = cio_websocket_location_handler_init(&handler, NULL, 0, on_connect, fake_handler_free);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

		struct cio_http_server server;
		server.on_error = test.on_error;

		struct cio_http_client client;
		client.parser.data = &server;
		fake_write_response_fake.custom_fake = write_response_call_callback_with_error;

		handler.websocket.ws_private.http_client = &client;
		handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
		handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
		handler.websocket.ws_private.http_client->write_response = fake_write_response;

		handler.websocket.ws_private.http_client->parser.upgrade = 1;
		handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
		handler.websocket.ws_private.http_client->http_major = 1;
		handler.websocket.ws_private.http_client->http_minor = 1;

		static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
		static const char sec_ws_version_value[] = "13";
		enum cio_http_cb_return cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

		static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
		static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
		cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

		cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SKIP_BODY, cb_ret, "on_header_complete returned wrong value");
		if (test.on_error != NULL) {
			TEST_ASSERT_EQUAL_MESSAGE(1, on_error_fake.call_count, "on_error was not called");
		}
	}
}

static void test_ws_location_sub_protocols(void)
{
	struct upgrade_test {
		const char *protocol_field;
		const char *protocol_value;
		enum cio_http_cb_return expected_ret_val;
		const char **sub_protocols;
		size_t num_sub_protocols;
	};

	const char *sub_protocols[] = {"echo", "jet"};

	struct upgrade_test tests[] = {
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jet", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jet,jetty", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jet, jetty", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jetty, jet", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jetty,\t jet", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    {.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jetty,\n jet", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "foo, bar", .expected_ret_val = CIO_HTTP_CB_ERROR, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "je", .expected_ret_val = CIO_HTTP_CB_ERROR, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "bar", .expected_ret_val = CIO_HTTP_CB_ERROR, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "foo", .protocol_value = "bar", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY, .sub_protocols = sub_protocols, .num_sub_protocols = ARRAY_SIZE(sub_protocols)},
	    //{.protocol_field = "foo", .protocol_value = "bar", .expected_ret_val = CIO_HTTP_CB_SKIP_BODY, .sub_protocols = NULL, .num_sub_protocols = 0},
	    //{.protocol_field = "Sec-WebSocket-Protocol", .protocol_value = "jet", .expected_ret_val = CIO_HTTP_CB_ERROR, .sub_protocols = NULL, .num_sub_protocols = 0},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tests); i++) {
		struct upgrade_test test = tests[i];

		struct cio_websocket_location_handler handler;

		enum cio_error err = cio_websocket_location_handler_init(&handler, test.sub_protocols, test.num_sub_protocols, on_connect, fake_handler_free);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "web socket handler initialization failed!");

		struct cio_http_client client;

		handler.websocket.ws_private.http_client = &client;
		handler.websocket.ws_private.http_client->current_handler = &handler.http_location;
		handler.websocket.ws_private.http_client->add_response_header = fake_add_response_header;
		handler.websocket.ws_private.http_client->write_response = fake_write_response;

		handler.websocket.ws_private.http_client->parser.upgrade = 1;
		handler.websocket.ws_private.http_client->http_method = CIO_HTTP_GET;
		handler.websocket.ws_private.http_client->http_major = 1;
		handler.websocket.ws_private.http_client->http_minor = 1;

		static const char sec_ws_version_field[] = "Sec-WebSocket-Version";
		static const char sec_ws_version_value[] = "13";
		enum cio_http_cb_return cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_version_field, sizeof(sec_ws_version_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_version_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_version_value, sizeof(sec_ws_version_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_version_value");

		static const char sec_ws_key_field[] = "Sec-WebSocket-Key";
		static const char sec_ws_key_value[] = "dGhlIHNhbXBsZSBub25jZQ==";
		cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, sec_ws_key_field, sizeof(sec_ws_key_field) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_key_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, sec_ws_key_value, sizeof(sec_ws_key_value) - 1);
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_key_value");

		cb_ret = handler.http_location.on_header_field_name(handler.websocket.ws_private.http_client, test.protocol_field, strlen(test.protocol_field));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_field returned wrong value for sec_ws_protocol_field");
		cb_ret = handler.http_location.on_header_field_value(handler.websocket.ws_private.http_client, test.protocol_value, strlen(test.protocol_value));
		TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_CB_SUCCESS, cb_ret, "on_header_value returned wrong value for sec_ws_protocol_value");

		cb_ret = handler.http_location.on_headers_complete(handler.websocket.ws_private.http_client);
		TEST_ASSERT_EQUAL_MESSAGE(test.expected_ret_val, cb_ret, "on_header_complete returned wrong value");
		if (test.expected_ret_val == CIO_HTTP_CB_SKIP_BODY) {
			TEST_ASSERT_EQUAL_MESSAGE(1, fake_write_response_fake.call_count, "write_response was not called");
			TEST_ASSERT_EQUAL_MESSAGE(1, on_connect_fake.call_count, "websocket on_connect was not called");
			TEST_ASSERT_EQUAL_MESSAGE(CIO_HTTP_STATUS_SWITCHING_PROTOCOLS, fake_write_response_fake.arg1_val, "write_response was not called with CIO_HTTP_STATUS_SWITCHING_PROTOCOLS");
		}

		setUp();
	}
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_ws_location_init_fails);
	RUN_TEST(test_ws_location_init_ok);
	RUN_TEST(test_ws_location_ws_init_fails);
	RUN_TEST(test_free_resources);

	RUN_TEST(test_ws_location_http_versions);
	;
	RUN_TEST(test_ws_location_wrong_http_method);
	RUN_TEST(test_ws_location_no_http_upgrade);
	RUN_TEST(test_ws_location_wrong_http_headers);
	RUN_TEST(test_ws_location_send_response_fails);
	RUN_TEST(test_ws_location_response_written_fails);

	RUN_TEST(test_ws_location_sub_protocols);

	return UNITY_END();
}
