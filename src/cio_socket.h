#ifndef CIO_SOCKET_H
#define CIO_SOCKET_H

#include "cio_io_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cio_socket {
	/**
	 * @brief The context pointer which is passed to the functions
	 * specified below.
	 */
	void *context;

	struct cio_io_stream *(*get_input_stream)(void *context);
	void (*close)(void *context);
};

#ifdef __cplusplus
}
#endif

#endif
