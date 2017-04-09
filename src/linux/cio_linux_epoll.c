#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_linux_epoll.h"

enum cio_error cio_linux_eventloop_init(struct cio_linux_eventloop_epoll *loop)
{
	loop->epoll_fd = epoll_create(1);
	if (loop->epoll_fd < 0) {
		return errno;
	}

	loop->go_ahead = true;

	return cio_success;
}

void eventloop_epoll_destroy(struct cio_linux_eventloop_epoll *loop)
{
	close(loop->epoll_fd);
}

enum cio_error cio_linux_eventloop_add(struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev)
{
	struct epoll_event epoll_ev = {0};

	epoll_ev.data.ptr = ev;
	epoll_ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	if (unlikely(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, ev->fd, &epoll_ev) < 0)) {
		return errno;
	}

	return cio_success;
}

enum cio_error cio_linux_eventloop_run(struct cio_linux_eventloop_epoll *loop)
{
	struct epoll_event *events = loop->epoll_events;

	while (likely(loop->go_ahead)) {
		int num_events =
		    epoll_wait(loop->epoll_fd, events, CONFIG_MAX_EPOLL_EVENTS, -1);

		if (unlikely(num_events < 0)) {
			if (errno == EINTR) {
				continue;
			}

			return errno;
		}

		for (unsigned int i = 0; i < (unsigned int)num_events; i++) {
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

