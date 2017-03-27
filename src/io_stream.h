#ifndef CIOL_IO_STREAM_H
#define CIOL_IO_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * A brief description.
 */


/**
 * @typedef void (*ciol_read_handler)(void *handler_context, enum ciol_error err, uint8_t *buf, size_t len)
 * @brief The read handler
 * 
 * @param handler_context the context
 */
typedef void (*ciol_read_handler)(void *handler_context, enum ciol_error err, uint8_t *buf, size_t len);

/**
 * @brief This structure describes the interface all implementations
 * have to fulfill.
 */
struct ciol_io_stream {
	/**
	 * @brief The context pointer which is passed to the
	 * ciol_io_stream::read and ciol_io_stream::close functions.
	 */
	void *context;

	/**
	 * @brief Read upto @p count bytes into the buffer @p buf starting
	 * with offset @p offset.
	 *
	 * @param context A pointer to the ciol_io_stream::context of the
	 * implementation implementing this interface.
	 * @param buf The buffer to be filled.
	 * @param offset The start offset in @p buf at which the data is
	 * written.
	 * @param count The maximum number of bytes to read.
	 * @param handler The callback function to be called when the read
	 * request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*read)(void *context, void *buf, size_t offset, size_t count, ciol_read_handler handler, void *handler_context);

	/**
	 * @brief Closes the stream
	 *
	 * Implementations implementing this interface are strongly
	 * encouraged to flush any write buffers and to free other resources
	 * associated with this stream.
	 */
	void (*close)(void *context);
};

#ifdef __cplusplus
}
#endif

#endif
