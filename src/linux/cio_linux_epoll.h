#ifndef CIO_LINUX_EPOLL_H
#define CIO_LINUX_EPOLL_H

#include <stdbool.h>
#include <sys/epoll.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_MAX_EPOLL_EVENTS 100

struct cio_linux_event_notifier  {
	void (*callback)(void *context);
	void *context;
	int fd;
	unsigned int event_counter;
};

struct cio_linux_eventloop_epoll {
	int epoll_fd;
	bool go_ahead;
	unsigned int loop_count;
	struct epoll_event epoll_events[CONFIG_MAX_EPOLL_EVENTS];
};

enum cio_error cio_linux_eventloop_init(struct cio_linux_eventloop_epoll *loop);
void cio_linux_eventloop_destroy(struct cio_linux_eventloop_epoll *loop);

enum cio_error cio_linux_eventloop_add(struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev);
enum cio_error cio_linux_eventloop_run(struct cio_linux_eventloop_epoll *loop);

#ifdef __cplusplus
}
#endif

#endif

