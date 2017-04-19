#include <sys/socket.h>
#include <sys/types.h>

#include "fff.h"
#include "unity.h"

#include "cio_linux_epoll.h"
#include "cio_linux_server_socket.h"

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, accept, int, struct sockaddr*, socklen_t*)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_linux_eventloop_epoll *, struct cio_linux_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, const struct cio_linux_eventloop_epoll *, struct cio_linux_event_notifier *)

void setUp(void)
{
	FFF_RESET_HISTORY();
	RESET_FAKE(accept);
}

static void test_accept(void) {
	accept_fake.return_val = 1;
	int fd = accept(1, NULL, NULL);
	TEST_ASSERT_EQUAL(1, fd);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_accept);
	return UNITY_END();
}
