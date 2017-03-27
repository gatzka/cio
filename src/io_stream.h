#ifndef CIOL_IO_STREAM_H
#define CIOL_IO_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*read_handler)(void *handler_context, enum ciol_error err, uint8_t *buf, size_t len);

struct io_stream {
	void *context;
	void (*read)(void *context, size_t num, read_handler handler, void *handler_context);
	void (*read_exactly)(void *context, size_t num, read_handler handler, void *handler_context);
	void (*read_until)(void *context, const char *delim, read_handler handler, void *handler_context);
	void (*close)(void *context);
};

#ifdef __cplusplus
}
#endif

#endif
