#ifndef CIO_ERROR_CODES_H
#define CIO_ERROR_CODES_H

#include "cio_system_error_code.h"

/**
 * \file
 * @brief This file contains the declaration of the enumeration for all
 * error codes.
 */

/**
 * @brief All Error codes used inside the cio libary.
 */
enum cio_error {
	cio_success = 0, /*!< No error occured. */
	cio_no_space_left_on_device = ENOSPC, /*!< No space left on device. */
	cio_permission_denied = EACCES, /*!< Permission denied. */
	cio_address_family_not_supported = EAFNOSUPPORT, /*!< Address family not supported. */
	cio_invalid_argument = EINVAL, /*!< Invalid argument. */
	cio_too_many_files_open = EMFILE, /*!< Too many files open. */
	cio_no_buffer_space = ENOBUFS, /*!< No buffer space. */
	cio_not_enough_memory = ENOMEM, /*!< Not enough memory. */
	cio_protocol_not_supported = EPROTONOSUPPORT, /*!< Protocol not supported. */
};

#ifdef __cplusplus
}
#endif

#endif

