#ifndef CIO_ERROR_CODES_H
#define CIO_ERROR_CODES_H

#include "system_error_code.h"

enum cio_error {
	success = 0,
	operation_would_block = EWOULDBLOCK,
	resource_unavailable_try_again = EAGAIN
};

#ifdef __cplusplus
}
#endif

#endif

