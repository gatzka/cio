#ifndef CIOL_ERROR_CODES_H
#define CIOL_ERROR_CODES_H

#include "system_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK 9901
#endif

#ifndef EAGAIN
#define EAGAIN 9902
#endif

enum ciol_error {
	operation_would_block = EWOULDBLOCK,
	resource_unavailable_try_again = EAGAIN
};

#ifdef __cplusplus
}
#endif

#endif

