#ifndef CIO_SERVER_SOCKET_LINUX_H
#define CIO_SERVER_SOCKET_LINUX_H

#include "cio_error_code.h"
#include "cio_server_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_server_socket_linux;

typedef void (*close_hook)(struct cio_server_socket_linux *ss);

const struct cio_server_socket *cio_server_socket_linux_init(struct cio_server_socket_linux *ss, close_hook close);

#ifdef __cplusplus
}
#endif

#endif
