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
	cio_address_family_not_supported = EAFNOSUPPORT, /*!< Address family not supported. */
	cio_address_in_use = EADDRINUSE, /*!< Address is already in use. */
	cio_address_not_available = EADDRNOTAVAIL, /*!< Address not available */
	cio_bad_address = EFAULT, /*!< Bad address. */
	cio_bad_file_descriptor = EBADF, /*!< Bad file descriptor. */
	cio_filename_too_long = ENAMETOOLONG, /*!< File name too long. */
	cio_invalid_argument = EINVAL, /*!< Invalid argument. */
	cio_no_buffer_space = ENOBUFS, /*!< No buffer space. */
	cio_no_protocol_option = ENOPROTOOPT, /*!< No protocol option. */
	cio_no_space_left_on_device = ENOSPC, /*!< No space left on device. */
	cio_no_such_file_or_directory = ENOENT, /*!< No such file or directory. */
	cio_not_a_directory = ENOTDIR, /*!< Not a directory. */
	cio_not_a_socket = ENOTSOCK, /*!< Not a socket. */
	cio_not_enough_memory = ENOMEM, /*!< Not enough memory. */
	cio_permission_denied = EACCES, /*!< Permission denied. */
	cio_protocol_not_supported = EPROTONOSUPPORT, /*!< Protocol not supported. */
	cio_read_only_file_system = EROFS, /*!< Read only file system. */
	cio_too_many_files_open = EMFILE, /*!< Too many files open. */
	cio_too_many_symbolic_link_levels = ELOOP, /*!< Too many symbolic link levels. */
};

#ifdef __cplusplus
}
#endif

#endif

