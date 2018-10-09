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

/**
 * @file
 * @brief This file contains the declarations all users of a
 * cio_websocket need to know.
 *
 * @warning Please note that you always have to wait for the completion of
 * a write call (@ref cio_websocket_write_message)
 * before issuing a new write call. Otherwise, you risk loss of data to be written!
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cio_buffered_stream.h"
#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_timer.h"
#include "cio_utf8_checker.h"
#include "cio_write_buffer.h"

struct cio_websocket;

typedef void (*cio_websocket_close_hook)(struct cio_websocket *s);
typedef void (*cio_websocket_on_connect)(struct cio_websocket *s);
typedef void (*cio_websocket_read_handler)(struct cio_websocket *ws, void *handler_context, enum cio_error err, uint8_t *data, size_t length, bool last_frame, bool is_binary);
typedef void (*cio_websocket_write_handler)(struct cio_websocket *ws, void *handler_context, enum cio_error err);

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

enum cio_websocket_frame_type {
	CIO_WEBSOCKET_CONTINUATION_FRAME = 0x0,
	CIO_WEBSOCKET_TEXT_FRAME = 0x1,
	CIO_WEBSOCKET_BINARY_FRAME = 0x2,
	CIO_WEBSOCKET_CLOSE_FRAME = 0x8,
	CIO_WEBSOCKET_PING_FRAME = 0x9,
	CIO_WEBSOCKET_PONG_FRAME = 0x0a,
};

#define CIO_WEBSOCKET_SMALL_FRAME_SIZE 125

struct cio_websocket_write_job {
	struct cio_websocket_write_job *next;
	struct cio_write_buffer *wbh;
	struct cio_write_buffer websocket_header;
	uint8_t send_header[14];
	cio_websocket_write_handler handler;
	void *handler_context;
	enum cio_websocket_frame_type frame_type;
	bool last_frame;
	cio_buffered_stream_write_handler stream_handler;
};

struct cio_response_buffer {
	struct cio_write_buffer wb_head;
	struct cio_write_buffer wb;
	uint8_t buffer[CIO_WEBSOCKET_SMALL_FRAME_SIZE];
};

struct cio_websocket_private {
	uint64_t read_frame_length;
	struct cio_eventloop *loop;

	struct {
		unsigned int fin : 1;
		unsigned int opcode : 4;
		unsigned int frag_opcode : 4;
		unsigned int self_initiated_close : 1;
		unsigned int is_server : 1;
		unsigned int fragmented_write : 1;
		unsigned int closed_by_error : 1;
        unsigned int to_be_closed;
	} ws_flags;

	cio_websocket_read_handler read_handler;
	void *read_handler_context;

	unsigned int in_user_writecallback_context;

	struct cio_buffered_stream *bs;
	struct cio_read_buffer *rb;
	struct cio_timer close_timer;
	struct cio_utf8_state utf8_state;
	cio_websocket_close_hook close_hook;
	uint8_t received_mask[4];

	struct cio_websocket_write_job write_message_job;
	struct cio_websocket_write_job write_ping_job;
	struct cio_websocket_write_job write_pong_job;
	struct cio_websocket_write_job write_close_job;

	struct cio_websocket_write_job *first_write_job;
	struct cio_websocket_write_job *last_write_job;

	struct cio_response_buffer close_buffer;
	struct cio_response_buffer ping_buffer;
};

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
	 * @return ::CIO_SUCCESS for success.
	 */
	enum cio_error (*close)(struct cio_websocket *ws, enum cio_websocket_status_code status, const char *reason, cio_websocket_write_handler handler, void *handler_context);

	/**
	 * @brief A pointer to a function which is called when a websocket connection was established.
	 *
	 * @param ws The websocket which was connected.
	 */
	void (*on_connect)(struct cio_websocket *ws);

	/**
	 * @anchor cio_websocket_read_message
	 * @brief Reads a message from a websocket.
	 *
	 * @param ws The websocket from which a message shall be read.
	 * @param handler A callback function that will be called when the read completes.
	 * @param handler_context A context pointer given to @p handler when called.
	 * @return ::CIO_SUCCESS for success.
	 */
	enum cio_error (*read_message)(struct cio_websocket *ws, cio_websocket_read_handler handler, void *handler_context);

	/**
	 * @anchor cio_websocket_write_message
	 * @brief Writes a complete message to the websocket.
	 *
	 * @warning Please note that the data @p payload encapsulates will be scrambled by
	 * the library if this function is used in a websocket client connection. So if
	 * you want to write the same data again, you have to re-initialize the data encapsluated
	 * by @p payload. In addition you should ALWAYS intialize the write buffer elements in
	 * @p payload using the @ref cio_write_buffer_element_init "non-const initialization function".
	 *
	 * @param ws The websocket which received the control frame.
	 * @param payload The payload to be sent.
	 * @param last_frame @c true if the is an unfragmented message or the last frame of a
	 * fragmented message, @c false otherwise.
	 * @param is_binary @c true if the message to be sent is a binary message.
	 * @param handler A callback function that will be called when the write completes.
	 * @param handler_context A context pointer given to @p handler when called.
	 * @return ::CIO_SUCCESS for success.
	 */
	enum cio_error (*write_message)(struct cio_websocket *ws, struct cio_write_buffer *payload, bool last_frame, bool is_binary, cio_websocket_write_handler handler, void *handler_context);

	/**
	 * @brief A pointer to a function which is called when a control frame was received.
	 *
	 * Library users are note required to set this function pointer.
	 * @param ws The websocket which received the control frame.
	 * @param kind The kind of control frame (::CIO_WEBSOCKET_CLOSE_FRAME, ::CIO_WEBSOCKET_PING_FRAME, ::CIO_WEBSOCKET_PONG_FRAME)
	 * @param data The data the control frame carried.
	 * @param length The length of data the control frame carried.
	 */
	void (*on_control)(const struct cio_websocket *ws, enum cio_websocket_frame_type kind, const uint8_t *data, uint_fast8_t length);

	/**
	 * @brief A pointer to a function which is called if a receive error occurred.
	 *
	 * Library users are note required to set this function pointer.
     * 
     * @warning If you set this function pointer, you are not allowed to
     * on this websocket any longer. (I.e., do not call
	 * @ref cio_websocket_close "close()" etc. on the websocket). Immediately after this
	 * function returns, the library closes the websocket on its own.
	 *
	 * @param ws The websocket which encountered the error.
	 * @param err An error code describing the error. Could be never ::CIO_SUCCESS.
	 * @param reason A string describing the reason for the error. Could be @c NULL.
	 */
	void (*on_error)(const struct cio_websocket *ws, enum cio_error err, const char *reason);

	/**
	 * @anchor cio_write_ping
	 * @brief Writes a ping frame to the websocket.
	 *
	 * @warning Please note that the data @p payload encapsulates will be scrambled by
	 * the library if this function is used in a websocket client connection. So if
	 * you want to write the same data again, you have to re-initialize the data encapsluated
	 * by @p payload. In addition you should ALWAYS intialize the write buffer elements in
	 * @p payload using the @ref cio_write_buffer_element_init "non-const initialization function".
	 *
	 * @param payload The payload to be sent.
	 * @param handler A callback function that will be called when the write operation of the ping completes.
	 * @param handler_context A context pointer given to @p handler when called.
	 * @return ::CIO_SUCCESS for success.
	 */
	enum cio_error (*write_ping)(struct cio_websocket *ws, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context);

	/**
	 * @anchor cio_write_pong
	 * @brief Writes a pong frame to the websocket.
	 *
	 * @warning Please note that the data @p payload encapsulates will be scrambled by
	 * the library if this function is used in a websocket client connection. So if
	 * you want to write the same data again, you have to re-initialize the data encapsluated
	 * by @p payload. In addition you should ALWAYS intialize the write buffer elements in
	 * @p payload using the @ref cio_write_buffer_element_init "non-const initialization function".
	 *
	 * @param payload The payload to be sent.
	 * @param handler A callback function that will be called when the write operation of the pong completes.
	 * @param handler_context A context pointer given to @p handler when called.
	 * @return ::CIO_SUCCESS for success.
	 */
	enum cio_error (*write_pong)(struct cio_websocket *ws, struct cio_write_buffer *payload, cio_websocket_write_handler handler, void *handler_context);

	/*! @cond PRIVATE */
	struct cio_websocket_private ws_private;
	/*! @endcond */
};

enum cio_error cio_websocket_init(struct cio_websocket *ws, bool is_server, cio_websocket_on_connect on_connect, cio_websocket_close_hook close_hook);

#ifdef __cplusplus
}
#endif

#endif
