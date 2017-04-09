#ifndef CIO_LINUX_SERVER_SOCKET_H
#define CIO_LINUX_SERVER_SOCKET_H

#include "cio_error_code.h"
#include "cio_server_socket.h"
#include "cio_linux_epoll.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_linux_server_socket;

typedef void (*close_hook)(struct cio_linux_server_socket *ss);

struct cio_linux_server_socket {
	struct cio_server_socket server_socket;
	int fd;
	close_hook close;
	cio_accept_handler handler;
	void *handler_context;
	struct cio_linux_event_notifier ev;
	struct cio_linux_eventloop_epoll *loop;
};

const struct cio_server_socket *cio_linux_server_socket_init(struct cio_linux_server_socket *ss,
                                                             struct cio_linux_eventloop_epoll *loop,
                                                             close_hook close);

#ifdef __cplusplus
}
#endif

#endif
