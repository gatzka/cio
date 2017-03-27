#ifndef CIO_LINUX_ERROR_CODES_H
#define CIO_LINUX_ERROR_CODES_H

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK 9901
#endif

#ifndef EAGAIN
#define EAGAIN 9902
#endif

#ifdef __cplusplus
}
#endif

#endif


