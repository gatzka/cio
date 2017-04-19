#include <sys/socket.h>
#include <sys/types.h>

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
FAKE_VALUE_FUNC(int, accept, int, struct sockaddr*, socklen_t*)

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
