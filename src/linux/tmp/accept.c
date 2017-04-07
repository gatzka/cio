#include "../cio_linux_epoll.h"
#include "../../cio_error_code.h"

int main()
{
	enum cio_error err = cio_linux_eventloop_init();
	(void)err;
	return 0;
}
