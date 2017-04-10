#ifndef CIO_LINUX_EPOLL_H
#define CIO_LINUX_EPOLL_H

#include <stdbool.h>
#include <sys/epoll.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Implementation of an event loop running on Linux using epoll.
 */

/**
 * @private
 */
#define CONFIG_MAX_EPOLL_EVENTS 100

/**
 * @brief The cio_linux_event_notifier struct
 */
struct cio_linux_event_notifier  {
	void (*callback)(void *context);
	void *context;
	int fd;
};

struct cio_linux_eventloop_epoll {
    /**
     * @privatesection
     */
	int epoll_fd;
	bool go_ahead;
	unsigned int event_counter;
	struct epoll_event epoll_events[CONFIG_MAX_EPOLL_EVENTS];
};

enum cio_error cio_linux_eventloop_init(struct cio_linux_eventloop_epoll *loop);
void cio_linux_eventloop_destroy(const struct cio_linux_eventloop_epoll *loop);

enum cio_error cio_linux_eventloop_add(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev);
void cio_linux_eventloop_remove(const struct cio_linux_eventloop_epoll *loop, struct cio_linux_event_notifier *ev);
enum cio_error cio_linux_eventloop_run(struct cio_linux_eventloop_epoll *loop);
void cio_linux_eventloop_cancel(struct cio_linux_eventloop_epoll *loop);

#ifdef __cplusplus
}
#endif

#endif

