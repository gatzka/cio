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
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_utf8_checker.h"
#include "cio_timer.h"
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

enum cio_websocket_status {
	CIO_WEBSOCKET_STATUS_OK = 0,
	CIO_WEBSOCKET_STATUS_CLOSED = -1
};

typedef void (*cio_websocket_write_handler)(struct cio_websocket *ws, void *handler_context, const struct cio_write_buffer *buffer, enum cio_error err);

#define CIO_WEBSOCKET_SMALL_FRAME_SIZE 125

struct cio_websocket {

	/**
	 * @anchor cio_websocket_close
	 * @brief Closes a websocket.
	 *
     * @warning Please note that the data @p reason encapsulates will be scrambled by
     * the library if this function is used in a websocket client connection. You should
     * ALWAYS intialize the write buffer elements in * @p reason using the
     * @ref cio_write_buffer_element_init "non-const initialization function".
	 *
	 * @param ws The websocket to be closed.
	 * @param status The @ref cio_websocket_status_code "websocket status code" to be sent.
	 * @param reason A buffer which contains the reason for the close in an UTF8 encoded string. Could be @c NULL if no reason should be sent.
	 */
	void (*close)(struct cio_websocket *ws, enum cio_websocket_status_code status, struct cio_write_buffer *reason);

	void (*on_connect)(struct cio_websocket *ws);

	/**
	 * @brief A pointer to a function which is called when a binary frame was received.
	 *
	 * @param ws The websocket which received the binary frame.
	 * @param data The data the binary frame carried.
	 * @param length The length of data the binary frame carried.
	 * @param last_frame Indicates if the last frame of a fragmented message is received.
	 * For unfragmented messages this flag is always set to @c true.
	 */
	void (*on_binaryframe)(struct cio_websocket *ws, uint8_t *data, size_t length, bool last_frame);

	/**
	 * @brief A pointer to a function which is called when a text frame was received.
	 *
	 * @param ws The websocket which received the text frame.
	 * @param data The data encoded in UTF8 the text frame carried. Please be aware that for
	 * fragmented messages the message boundary could be within an UTF8 code point.
	 * @param length The length of data the text frame carried.
	 * @param last_frame Indicates if the last frame of a fragmented message is received.
	 * For unfragmented messages this flag is always set to @c true.
	 */
	void (*on_textframe)(struct cio_websocket *ws, uint8_t *data, size_t length, bool last_frame);

	/**
	 * @brief A pointer to a function which is called if a ping frame was received.
	 *
	 * Library users are note required to set this function pointer. If you set
	 * this function pointer, please do NOT sent a pong frame as a response.
	 * The library takes care of this already.
	 * @param ws The websocket which received the ping frame.
	 * @param data The data the ping frame carried.
	 * @param length The length of data the ping frame carried.
	 */
	void (*on_ping)(struct cio_websocket *ws, const uint8_t *data, size_t length);

	/**
	 * @brief A pointer to a function which is called if a pong frame was received.
	 *
	 * Library users are note required to set this function pointer.
	 * @param ws The websocket which received the pong frame.
	 * @param data The data the pong frame carried.
	 * @param length The length of data the pong frame carried.
	 */
	void (*on_pong)(struct cio_websocket *ws, uint8_t *data, size_t length);

	/**
	 * @brief A pointer to a function which is called when a close frame was received.
	 *
	 * Library users are note required to set this function pointer. If you set this
	 * function pointer, please do NOT @ref cio_websocket_close "close()" the
	 * websocket in the callback function. This is done immediately by the library after return of
	 * this function.
	 *
	 * @param ws The websocket which received the pong frame.
	 * @param data The data the pong frame carried.
	 * @param length The length of data the pong frame carried.
	 */
	void (*on_close)(const struct cio_websocket *ws, enum cio_websocket_status_code status, const char *reason, size_t reason_length);

	/**
	 * @brief A pointer to a function which is called if a receive error occurred.
	 *
	 * Library users are note required to set this function pointer. If you set this
	 * function pointer, please operate no longer on this websocket (Do not call
	 * @ref cio_websocket_close "close()" etc. on the websocket). Immediately after this
	 * function returns, the library closes the websocket on its own.
	 *
	 * @param ws The websocket which encountered the error.
	 * @param status The status code describing the error.
	 * @param status A string describing the reason for the error. Could be @c NULL.
	 */
	void (*on_error)(const struct cio_websocket *ws, enum cio_websocket_status_code status, const char *reason);

	/**
     * @brief Writes a text frame to the websocket.
     *
     * @warning Please note that the data @p payload encapsulates will be scrambled by
     * the library if this function is used in a websocket client connection. So if
     * you want to write the same data again, you have to re-initialize the data encapsluated
     * by @p payload. In addition you should ALWAYS intialize the write buffer elements in
     * @p payload using the @ref cio_write_buffer_element_init "non-const initialization function".
     *
     * @warning If you call this function consecutively without waiting that @p handler is called,
     * you should know that @p handler is called only for the last write call!
     *
     * @param payload The payload to be sent.
     * @param last_frame @c true if the is an unfragmented message or the last frame of a
     * fragmented message, @c false otherwise.
     * @param handler A callback function that will be called when the write completes.
     * @param handler_context A context pointer given to @p handler when called.
     */
	enum cio_websocket_status (*write_textframe)(struct cio_websocket *ws, struct cio_write_buffer *payload, bool last_frame, cio_websocket_write_handler handler, void *handler_context);

	/**
     * @brief Writes a binary frame to the websocket.
     *
     * @warning Please note that the data @p payload encapsulates will be scrambled by
     * the library if this function is used in a websocket client connection. So if
     * you want to write the same data again, you have to re-initialize the data encapsluated
     * by @p payload. In addition you should ALWAYS intialize the write buffer elements in
     * @p payload using the @ref cio_write_buffer_element_init "non-const initialization function".
     *
     * @warning If you call this function consecutively without waiting that @p handler is called,
     * you should know that @p handler is called only for the last write call!
     *
     * @param payload The payload to be sent.
     * @param last_frame @c true if the is an unfragmented message or the last frame of a
     * fragmented message, @c false otherwise.
     * @param handler A callback function that will be called when the write completes.
     * @param handler_context A context pointer given to @p handler when called.
     */
	enum cio_websocket_status (*write_binaryframe)(struct cio_websocket *ws, struct cio_write_buffer *payload, bool last_frame, cio_websocket_write_handler handler, void *handler_context);

	enum cio_websocket_status (*write_pingframe)(struct cio_websocket *ws, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context);

	/*! @cond PRIVATE */

	void (*internal_on_connect)(struct cio_websocket *ws);
	uint64_t read_frame_length;
	struct cio_eventloop *loop;

	struct {
		unsigned int fin : 1;
		unsigned int rsv : 3;
		unsigned int opcode : 4;
		unsigned int shall_mask : 1;
		unsigned int frag_opcode : 4;
		unsigned int is_fragmented : 1;
		unsigned int self_initiated_close : 1;
		unsigned int to_be_closed : 1;
		unsigned int is_server : 1;
		unsigned int writing_frame : 1;
		unsigned int handle_frame_ctx : 1;
	} ws_flags;

	cio_websocket_write_handler write_handler;
	void *write_handler_context;

	cio_websocket_write_handler write_ping_handler;
	void *write_ping_handler_context;

	struct cio_buffered_stream *bs;
	struct cio_read_buffer *rb;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb_send_header;
	struct cio_write_buffer wb_close_status;
	struct cio_write_buffer wb_control_data;
	struct cio_timer close_timer;
	struct cio_utf8_state utf8_state;
	cio_websocket_close_hook close_hook;
	cio_buffered_stream_write_handler user_write_handler;
	uint8_t mask[4];
	uint8_t send_header[14];
	uint16_t close_status;
	uint8_t send_control_frame_buffer[CIO_WEBSOCKET_SMALL_FRAME_SIZE];
	/*! @endcond */
};

void cio_websocket_init(struct cio_websocket *ws, bool is_server, cio_websocket_close_hook close_hook);

#ifdef __cplusplus
}
#endif

#endif
