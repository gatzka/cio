#include <stddef.h>
#include <stdio.h>

#include "../cio_linux_epoll.h"
#include "../cio_linux_server_socket.h"
#include "../../cio_error_code.h"

static void accept_handler(void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)handler_context;
	(void)socket;
	(void)err;
	printf("Angekommen!\n");
}

int main()
{
	int go_ahead = 1;
	cio_linux_eventloop_init();

	struct cio_linux_server_socket ss_linux;

	const struct cio_server_socket *ss = cio_server_socket_linux_init(&ss_linux, NULL);
	ss->init(ss->context, 12345, 5, NULL);
	ss->accept(ss->context, accept_handler, NULL);
	cio_linux_eventloop_run(&go_ahead);
	return 0;
}
