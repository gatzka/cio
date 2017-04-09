#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_linux_epoll.h"

#define CONFIG_MAX_EPOLL_EVENTS 100

static int epoll_fd = -1;

enum cio_error cio_linux_eventloop_init(void)
{
	epoll_fd = epoll_create(1);
	if (epoll_fd < 0) {
		return errno;
	}

	return cio_success;;
}

void eventloop_epoll_destroy(void)
{
	close(epoll_fd);
}

enum cio_error cio_linux_eventloop_add(struct cio_linux_event_notifier *ev)
{
	struct epoll_event epoll_ev = {0};

	epoll_ev.data.ptr = ev;
	epoll_ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	if (unlikely(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ev->fd, &epoll_ev) < 0)) {
		return errno;
	}

	return cio_success;
}

enum cio_error cio_linux_eventloop_run(const int *go_ahead)
{
	struct epoll_event events[CONFIG_MAX_EPOLL_EVENTS];

	while (likely(*go_ahead)) {
		int num_events =
		    epoll_wait(epoll_fd, events, CONFIG_MAX_EPOLL_EVENTS, -1);

		if (unlikely(num_events < 0)) {
			if (errno == EINTR) {
				continue;
			}

			return errno;
		}

		for (int i = 0; i < num_events; i++) {
			struct cio_linux_event_notifier *ev = events[i].data.ptr;

			if (unlikely((events[i].events & ~(EPOLLIN | EPOLLOUT)) != 0)) {
				// TODO
			} else {
				if (events[i].events & (EPOLLIN | EPOLLOUT)) {
					ev->callback(ev->context);
				}
			}
		}
	}

	return cio_success;
}

