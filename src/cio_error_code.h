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
	CIO_EOF = 1,                                      /*!< End of File occured. */
	CIO_SUCCESS = 0,                                  /*!< No error occured. */
	CIO_ADDRESS_FAMILY_NOT_SUPPORTED = -EAFNOSUPPORT, /*!< Address family not supported. */
	CIO_ADDRESS_IN_USE = -EADDRINUSE,                 /*!< Address is already in use. */
	CIO_ADDRESS_NOT_AVAILABLE = -EADDRNOTAVAIL,       /*!< Address not available */
	CIO_BAD_ADDRESS = -EFAULT,                        /*!< Bad address. */
	CIO_BAD_FILE_DESCRIPTOR = -EBADF,                 /*!< Bad file descriptor. */
	CIO_FILE_EXISTS = -EEXIST,                        /*!< File exists. */
	CIO_FILENAME_TOO_LONG = -ENAMETOOLONG,            /*!< File name too long. */
	CIO_INVALID_ARGUMENT = -EINVAL,                   /*!< Invalid argument. */
	CIO_NO_BUFFER_SPACE = -ENOBUFS,                   /*!< No buffer space. */
	CIO_NO_PROTOCOL_OPTION = -ENOPROTOOPT,            /*!< No protocol option. */
	CIO_NO_SPACE_LEFT_ON_DEVICE = -ENOSPC,            /*!< No space left on device. */
	CIO_AGAIN = -EAGAIN,                              /*!< Try again. */
	CIO_NO_SUCH_FILE_OR_DIRECTORY = -ENOENT,          /*!< No such file or directory. */
	CIO_NOT_A_DIRECTORY = -ENOTDIR,                   /*!< Not a directory. */
	CIO_NOT_A_SOCKET = -ENOTSOCK,                     /*!< Not a socket. */
	CIO_NOT_ENOUGH_MEMORY = -ENOMEM,                  /*!< Not enough memory. */
	CIO_OPERATION_NOT_PERMITTED = -EPERM,             /*!< Operation not permitted. */
	CIO_PERMISSION_DENIED = -EACCES,                  /*!< Permission denied. */
	CIO_PROTOCOL_NOT_SUPPORTED = -EPROTONOSUPPORT,    /*!< Protocol not supported. */
	CIO_READ_ONLY_FILE_SYSTEM = -EROFS,               /*!< Read only file system. */
	CIO_TOO_MANY_FILES_OPEN = -EMFILE,                /*!< Too many files open. */
	CIO_TOO_MANY_SYMBOLIC_LINK_LEVELS = -ELOOP,       /*!< Too many symbolic link levels. */
	CIO_OPERATION_ABORTED = -ECANCELED,               /*!< Operation cancelled. */
	CIO_NO_SUCH_DEVICE = -ENODEV,                     /*!< No such device. */
	CIO_MESSAGE_TOO_LONG = -EMSGSIZE,                 /*!< Message too long. */
	CIO_BROKEN_PIPE = -EPIPE,                         /*!< Broken Pipe. */
	CIO_NETRESET = -ENETRESET                         /*!< Network dropped connection because of reset. */
};

static inline bool cio_is_error(enum cio_error error)
{
	return error < CIO_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif
