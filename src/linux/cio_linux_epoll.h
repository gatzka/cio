#ifndef CIO_LINUX_EPOLL_H
#define CIO_LINUX_EPOLL_H

#ifdef __cplusplus
extern "C" {
#endif

struct cio_linux_event_notifier  {
	void (*callback)(void *context);
	void *context;
	int fd;
};

enum cio_error cio_linux_eventloop_init(void);
void eventloop_epoll_destroy(void);

enum cio_error cio_linux_eventloop_add(struct cio_linux_event_notifier *ev);
enum cio_error cio_linux_eventloop_run(const int *go_ahead);

#ifdef __cplusplus
}
#endif

#endif

