#ifndef CIO_LINUX_SERVER_SOCKET_H
#define CIO_LINUX_SERVER_SOCKET_H

#include "cio_error_code.h"
#include "cio_linux_epoll.h"
#include "cio_server_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * @brief The linux specific implementation of a cio_server_socket.
 */

struct cio_linux_server_socket;

/**
 * @brief The type  of close hook function.
 *
 * @param ss The cio_linux_server_socket the close hook was called on.
 */
typedef void (*close_hook)(struct cio_linux_server_socket *ss);

/**
 * @brief Structure describing a linux server socket.
 *
 * All members of this structure shall be considered private
 * and not be used by user of the cio library.
 */
struct cio_linux_server_socket {
    /**
     * @privatesection
     */
	struct cio_server_socket server_socket;
	int fd;
	close_hook close;
	cio_accept_handler handler;
	void *handler_context;
	struct cio_linux_event_notifier ev;
	struct cio_linux_eventloop_epoll *loop;
};


/**
 * @brief Initializes a cio_linux_server_socket.
 *
 * @param ss The cio_linux_server socket that should be initialized.
 * @param loop The event loop the server socket shall operate on.
 * @param close A close hook function. If this parameter is non @p NULL,
 * the function will be called directly after
 * @ref cio_server_socket_close "closing" the cio_server_socket.
 * It is guaranteed the the cio library will not access any memory of
 * cio_linux_server_socket that is passed to the close hook. Therefore
 * the hook could be used to free the memory of the linux server socket.
 * @return The cio_server_socket which shall be used after initializing.
 */
const struct cio_server_socket *cio_linux_server_socket_init(struct cio_linux_server_socket *ss,
                                                             struct cio_linux_eventloop_epoll *loop,
                                                             close_hook close);

#ifdef __cplusplus
}
#endif

#endif
