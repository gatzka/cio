#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_timer.h"

static struct cio_eventloop loop;

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void handle_timeout(void *handler_context, enum cio_error err)
{
	(void)handler_context;
	if (err == cio_success) {
		fprintf(stdout, "timer expired!\n");
	} else {
		fprintf(stdout, "timer error!\n");
	}
}

int main()
{
	int ret = EXIT_SUCCESS;

	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		return -1;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		signal(SIGTERM, SIG_DFL);
		return -1;
	}

	enum cio_error err = cio_eventloop_init(&loop);
	if (err != cio_success) {
		return EXIT_FAILURE;
	}

	struct cio_timer timer;
	err = cio_timer_init(&timer, &loop, NULL);
	if (err != cio_success) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	timer.expires_from_now(&timer, 5000000000, handle_timeout, NULL);

	err = cio_eventloop_run(&loop);
	if (err != cio_success) {
		fprintf(stderr, "error in cio_eventloop_run!\n");
	}

destroy_loop:
	cio_eventloop_destroy(&loop);

	return ret;
}
