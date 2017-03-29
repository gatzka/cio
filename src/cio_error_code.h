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
};

#ifdef __cplusplus
}
#endif

#endif

