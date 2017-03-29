#ifndef CIO_STREAM_HANDLER_H
#define CIO_STREAM_HANDLER_H

#include <stdint.h>

#include "cio_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * @brief Definition of the stream callback function type.
 * need to know.
 */

/**
 * @brief The type of a callback function passed to all stream functions.
 * 
 * @param handler_context The context the functions works on.
 * @param err If err != ::cio_success, the read failed.
 * @param buf A pointer to the begin of the buffer where the data was read in. 
 * @param bytes_transferred The number of bytes transferred into @p buf.
 */
typedef void (*cio_stream_handler)(void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred);

#ifdef __cplusplus
}
#endif

#endif
