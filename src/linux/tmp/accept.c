#include <stddef.h>
#include <signal.h>
#include <stdio.h>

#include "../cio_linux_epoll.h"
#include "../cio_linux_server_socket.h"
#include "../cio_server_socket.h"
#include "../../cio_error_code.h"

static struct cio_linux_eventloop_epoll loop;

static void accept_handler(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)handler_context;
	(void)err;
	(void)socket;
	ss->close(ss);
	printf("Angekommen!\n");
}

static void sighandler(int signum)
{
	(void)signum;
	cio_linux_eventloop_cancel(&loop);
}

static int register_signal_handler(void)
{
	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		//log_err("installing signal handler for SIGTERM failed!\n");
		return -1;
	}
	if (signal(SIGINT, sighandler) == SIG_ERR) {
		//log_err("installing signal handler for SIGINT failed!\n");
		signal(SIGTERM, SIG_DFL);
		return -1;
	}
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		//log_err("ignoring SIGPIPE failed!\n");
		return -1;
	}

	return 0;
}

int main()
{
	register_signal_handler();
	cio_linux_eventloop_init(&loop);

	struct cio_linux_server_socket ss_linux;

	const struct cio_server_socket *ss = cio_linux_server_socket_init(&ss_linux, &loop, NULL);




	ss->init(ss->context, 12345, 5, NULL);
	ss->accept(ss->context, accept_handler, NULL);



	cio_linux_eventloop_run(&loop);

	cio_linux_eventloop_destroy(&loop);
	return 0;
}
