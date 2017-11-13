/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2017> <Stephan Gatzka>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef CIO_ERROR_CODES_H
#define CIO_ERROR_CODES_H

#include <stdbool.h>

#include "cio_error_code_impl.h"

/**
 * @file
 * @brief This file contains the declaration of the enumeration for all
 * error codes.
 */

/**
 * @brief All error codes used inside the cio libary.
 */
enum cio_error {
	cio_eof = 1,                                      /*!< End of File occured. */
	cio_success = 0,                                  /*!< No error occured. */
	cio_address_family_not_supported = -EAFNOSUPPORT, /*!< Address family not supported. */
	cio_address_in_use = -EADDRINUSE,                 /*!< Address is already in use. */
	cio_address_not_available = -EADDRNOTAVAIL,       /*!< Address not available */
	cio_bad_address = -EFAULT,                        /*!< Bad address. */
	cio_bad_file_descriptor = -EBADF,                 /*!< Bad file descriptor. */
	cio_file_exists = -EEXIST,                        /*!< File exists. */
	cio_filename_too_long = -ENAMETOOLONG,            /*!< File name too long. */
	cio_invalid_argument = -EINVAL,                   /*!< Invalid argument. */
	cio_no_buffer_space = -ENOBUFS,                   /*!< No buffer space. */
	cio_no_protocol_option = -ENOPROTOOPT,            /*!< No protocol option. */
	cio_no_space_left_on_device = -ENOSPC,            /*!< No space left on device. */
	cio_again = -EAGAIN,                              /*!< Try again. */
	cio_no_such_file_or_directory = -ENOENT,          /*!< No such file or directory. */
	cio_not_a_directory = -ENOTDIR,                   /*!< Not a directory. */
	cio_not_a_socket = -ENOTSOCK,                     /*!< Not a socket. */
	cio_not_enough_memory = -ENOMEM,                  /*!< Not enough memory. */
	cio_operation_not_permitted = -EPERM,             /*!< Operation not permitted. */
	cio_permission_denied = -EACCES,                  /*!< Permission denied. */
	cio_protocol_not_supported = -EPROTONOSUPPORT,    /*!< Protocol not supported. */
	cio_read_only_file_system = -EROFS,               /*!< Read only file system. */
	cio_too_many_files_open = -EMFILE,                /*!< Too many files open. */
	cio_too_many_symbolic_link_levels = -ELOOP,       /*!< Too many symbolic link levels. */
	cio_operation_aborted = -ECANCELED,               /*!< Operation cancelled. */
	cio_no_such_device = -ENODEV,                     /*!< No such device. */
	cio_message_too_long = -EMSGSIZE                  /*!< Message too long. */
};

static inline bool cio_is_error(enum cio_error error)
{
	return error < cio_success;
}

#ifdef __cplusplus
}
#endif

#endif
