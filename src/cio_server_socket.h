#ifndef CIO_SERVER_SOCKET_H
#define CIO_SERVER_SOCKET_H

#include <stdint.h>

#include "cio_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cio_accept_handler)(void *handler_context, enum cio_error err, struct cio_socket *socket);

struct cio_server_socket {
	/**
	 * @brief The context pointer which is passed to the functions
	 * specified below.
	 */
	void *context;

	void (*init)(void *context, uint16_t port, unsigned int backlog);

	void (*accept)(void *context, cio_accept_handler handler, void *handler_context);

	void (*close)(void *context);
};

#ifdef __cplusplus
}
#endif

#endif
