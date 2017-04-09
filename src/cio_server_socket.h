#ifndef CIO_SERVER_SOCKET_H
#define CIO_SERVER_SOCKET_H

#include <stdint.h>

#include "cio_error_code.h"
#include "cio_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the interface of a server socket.
 *
 * A server socket can be @ref cio_server_socket_init "initialized",
 * @ref cio_server_socket_accept "accept connections" and can be
 * @ref cio_server_socket_close "closed".
 */

struct cio_server_socket;

typedef void (*cio_accept_handler)(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket);

/**
 * @brief The cio_server_socket struct describes a server socket.
 */
struct cio_server_socket {
	/**
	 * @brief The context pointer which is passed to the functions
	 * specified below.
	 */
	void *context;

    /**
     * @anchor cio_server_socket_init
     * @brief Initializes a cio_server_socket.
     *
     * @param context The cio_server_socket::context.
     * @param port The TCP port the cio_server_socket shall listen on.
     * @param backlog The minimal length of the listen queue.
     * @param bind_address The IP address a cio_server_socket shall bound to.
     * If @a bind_address is @p NULL, cio_server_socket will bind to any interface.
     */
	enum cio_error (*init)(void *context, uint16_t port, unsigned int backlog, const char *bind_address);

    /**
     * @anchor cio_server_socket_accept
     * @brief Accepts an incoming socket connection.
     *
     * @param context The cio_server_socket::context.
     * @param handler The function to be called if the accept failes or succeeds.
     * @param handler_context The context passed the the @a handler function.
     */
	void (*accept)(void *context, cio_accept_handler handler, void *handler_context);

    /**
     * @anchor cio_server_socket_close
     * @brief Closes the cio_server_socket.
     *
     * @param context The cio_server_socket::context.
     */
	void (*close)(void *context);
};

#ifdef __cplusplus
}
#endif

#endif
