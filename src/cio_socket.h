#ifndef CIO_SOCKET_H
#define CIO_SOCKET_H

#include "cio_io_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the interface of a socket.
 *
 * You can @ref cio_socket_get_input_stream "get an input stream"
 * from a socket or @ref cio_socket_close "close" the socket.
 */

struct cio_socket {
	/**
	 * @brief The context pointer which is passed to the functions
	 * specified below.
	 */
	void *context;

	/**
	 * @anchor cio_socket_get_input_stream
	 * @brief Closes the cio_server_socket.
	 *
	 * @param context The cio_server_socket::context.
	 */
	struct cio_io_stream *(*get_input_stream)(void *context);

	/**
	 * @anchor cio_socket_close
	 * @brief Closes the cio_socket.
	 *
	 * Once a socket has been closed, no further communication is possible. Closing the socket
	 * also closes the socket's cio_io_stream.
	 *
	 * @param context The cio_server_socket::context.
	 */
	void (*close)(void *context);
};

#ifdef __cplusplus
}
#endif

#endif
