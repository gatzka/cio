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

#ifndef CIO_WEBSOCKET_H
#define CIO_WEBSOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "cio_buffered_stream.h"
#include "cio_write_buffer.h"

struct cio_websocket;

typedef void (*cio_websocket_close_hook)(struct cio_websocket *s);

enum cio_websocket_status_code {
	CIO_WEBSOCKET_CLOSE_NORMAL = 1000,
	CIO_WEBSOCKET_CLOSE_GOING_AWAY = 1001,
	CIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR = 1002,
	CIO_WEBSOCKET_CLOSE_UNSUPPORTED = 1003,
	CIO_WEBSOCKET_CLOSE_NO_STATUS = 1005,
	CIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA = 1007,
	CIO_WEBSOCKET_CLOSE_POLICY_VIOLATION = 1008,
	CIO_WEBSOCKET_CLOSE_TOO_LARGE = 1009,
	CIO_WEBSOCKET_CLOSE_MISSING_EXTENSION = 1010,
	CIO_WEBSOCKET_CLOSE_INTERNAL_ERROR = 1011,
	CIO_WEBSOCKET_CLOSE_SERVICE_RESTART = 1012,
	CIO_WEBSOCKET_CLOSE_TRY_AGAIN_LATER = 1013,
	CIO_WEBSOCKET_CLOSE_TLS_HANDSHAKE = 1015,
	CIO_WEBSOCKET_CLOSE_RESERVED_LOWER_BOUND = 3000,
	CIO_WEBSOCKET_CLOSE_RESERVED_UPPER_BOUND = 4999
};

typedef void (*cio_websocket_onconnect_handler)(struct cio_websocket *ws);

struct cio_websocket {
	void (*close)(struct cio_websocket *ws);

	cio_websocket_onconnect_handler onconnect_handler;

	/**
	 * @privatesection
	 */

	struct cio_buffered_stream *bs;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb_send_header;
	struct cio_write_buffer wb_send_payload;
	cio_websocket_close_hook close_hook;
	bool is_server;
	bool wait_for_close_response;
	uint16_t close_status;
	uint8_t send_header[14];
};


void cio_websocket_init(struct cio_websocket *ws, bool is_server, cio_websocket_close_hook close_hook);

#ifdef __cplusplus
}
#endif

#endif
