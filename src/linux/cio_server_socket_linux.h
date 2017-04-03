#ifndef CIO_SERVER_SOCKET_LINUX_H
#define CIO_SERVER_SOCKET_LINUX_H

#include "cio_error_code.h"
#include "cio_server_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_server_socket_linux {
	struct cio_server_socket server_socket;
	int fd;
	void (*close_hook)(struct cio_server_socket_linux *context);
};

enum cio_error init(void *context, uint16_t port, unsigned int backlog, const char *bind_address);

#ifdef __cplusplus
}
#endif

#endif
