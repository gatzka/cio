#ifndef CIO_IO_VECTOR_H
#define CIO_IO_VECTOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * @brief This file contains the of a scatter/gather I/O buffer element.
 */

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

#ifdef __cplusplus
}
#endif

#endif
