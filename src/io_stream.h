#ifndef CIO_IO_STREAM_H
#define CIO_IO_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * @brief This file contains the definitions all users of a cio_io_stream
 * need to know.
 */

/**
 * @brief The type of a callback function passed to cio_io_stream::read.
 * 
 * @param handler_context The context the functions works on.
 * @param err If err != ::cio_success, the read failed.
 * @param buf A pointer to the begin of the buffer where the data was read in. 
 * @param bytes_transferred The number of bytes transferred into @p buf.
 */
typedef void (*cio_stream_handler)(void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred);

/**
 * @brief Type to represent an element of an I/O vector.
 */
struct cio_io_vector {
	/**
	 * Start address of a buffer.
	 */
	const void *iov_base;

	/**
	 * Length of the buffer.
	 */
	size_t iov_len;
};

/**
 * @brief This structure describes the interface all implementations
 * have to fulfill.
 */
struct cio_io_stream {
	/**
	 * @brief The context pointer which is passed to the
	 * cio_io_stream::read and cio_io_stream::close functions.
	 */
	void *context;

	/**
	 * @brief Read upto @p count bytes into the buffer @p buf starting
	 * with offset @p offset.
	 *
	 * @param context A pointer to the cio_io_stream::context of the
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
	void (*read)(void *context, void *buf, size_t offset, size_t count, cio_stream_handler handler, void *handler_context);

	/**
	 * @brief Writes @p count buffers to the stream.
	 *
	 * @param context A pointer to the cio_io_stream::context of the
	 * implementation implementing this interface.
	 * @param io_vec An array of cio_io_vector buffers.
	 * @param count Number of cio_io_vector buffers in @p io_vec.
	 * @param handler The callback function to be called when the write
	 * request is (partly) fulfilled.
	 * @param handler_context A pointer to a context which might be
	 * useful inside @p handler
	 */
	void (*writev)(void *context, struct cio_io_vector *io_vec, unsigned int count, cio_stream_handler handler, void *handler_context);

	/**
	 * @brief Closes the stream.
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
