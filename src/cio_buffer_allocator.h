#ifndef CIO_BUFFER_ALLOCATOR_H
#define CIO_BUFFER_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * @brief This file contains the declarations all users of a buffer_allocator
 * need to know.
 */

struct cio_buffer {
	/**
	 * The address of the buffer.
	 */
	void *address;

	/**
	 * The size of the buffer in bytes.
	 */
	size_t size;
};

/**
 * Interface description for a buffer allocator.
 */
struct cio_buffer_allocator {
	/**
	 * @brief The context pointer which is passed to the functions
	 * specified below.
	 */
	void *context;

	/**
	 * @brief Allocates a buffer.
	 *
	 * @param context A pointer to the cio_buffer_allocator::context of the
	 * implementation implementing this interface.
	 * @param size The requested size of the buffer. The allocated
	 * buffer might be greater than requested.
	 * @return A cio_buffer containing the address of the buffer and the
	 * real size of the buffer.If the request can't be fulfilled,
	 * cio_buffer::address is @p NULL.
	 */
	struct cio_buffer (*alloc)(void *context, size_t size);

	/**
	 * @brief Frees a buffer.
	 *
	 * @param context A pointer to the cio_buffer_allocator::context of the
	 * implementation implementing this interface.
	 * @param ptr Address of the buffer to be freed.
	 */
	void (*free)(void *context, void *ptr);
};

#ifdef __cplusplus
}
#endif

#endif
