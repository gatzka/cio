#ifndef CIOL_ERROR_CODES_H
#define CIOL_ERROR_CODES_H

#include "system_error_code.h"

enum ciol_error {
	success = 0,
	operation_would_block = EWOULDBLOCK,
	resource_unavailable_try_again = EAGAIN
};

#ifdef __cplusplus
}
#endif

#endif

