#ifndef CIO_SERVER_SOCKET_H
#define CIO_SERVER_SOCKET_H

#include <stdint.h>

#include "cio_error_code.h"
#include "cio_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_server_socket;

typedef void (*cio_accept_handler)(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket);

struct cio_server_socket {
	/**
	 * @brief The context pointer which is passed to the functions
	 * specified below.
	 */
	void *context;

	enum cio_error (*init)(void *context, uint16_t port, unsigned int backlog, const char *bind_address);

	void (*accept)(void *context, cio_accept_handler handler, void *handler_context);

	void (*close)(void *context);
};

#ifdef __cplusplus
}
#endif

#endif
