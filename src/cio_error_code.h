/*
 * SPDX-License-Identifier: MIT
 *
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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief This file contains the declaration of the enumeration for all
 * error codes.
 */

/**
 * @brief All error codes used inside the cio libary.
 */
enum cio_error {
	CIO_EOF = 1,                                                                                /*!< End of File occured. */
	CIO_SUCCESS = 0,                                                                            /*!< No error occured. */
	CIO_ADDRESS_FAMILY_NOT_SUPPORTED = -CIO_SOCKET_ERROR(EAFNOSUPPORT),                         /*!< Address family not supported. */
	CIO_ADDRESS_IN_USE = -CIO_SOCKET_ERROR(EADDRINUSE),                                         /*!< Address is already in use. */
	CIO_ADDRESS_NOT_AVAILABLE = -CIO_SOCKET_ERROR(EADDRNOTAVAIL),                               /*!< Address not available */
	CIO_BAD_ADDRESS = -CIO_SOCKET_ERROR(EFAULT),                                                /*!< Bad address. */
	CIO_BAD_FILE_DESCRIPTOR = -CIO_SOCKET_ERROR(EBADF),                                         /*!< Bad file descriptor. */
	CIO_FILENAME_TOO_LONG = -CIO_SOCKET_ERROR(ENAMETOOLONG),                                    /*!< File name too long. */
	CIO_INVALID_ARGUMENT = -CIO_SOCKET_ERROR(EINVAL),                                           /*!< Invalid argument. */
	CIO_NO_BUFFER_SPACE = -CIO_SOCKET_ERROR(ENOBUFS),                                           /*!< No buffer space. */
	CIO_NO_PROTOCOL_OPTION = -CIO_SOCKET_ERROR(ENOPROTOOPT),                                    /*!< No protocol option. */
	CIO_WOULDBLOCK = -CIO_SOCKET_ERROR(EWOULDBLOCK),                                            /*!< Try again. */
	CIO_NOT_A_SOCKET = -CIO_SOCKET_ERROR(ENOTSOCK),                                             /*!< Not a socket. */
	CIO_OPERATION_NOT_PERMITTED = -CIO_WINDOWS_UNIX_SOCKET_ERROR(ERROR_ACCESS_DENIED, EPERM),   /*!< Operation not permitted. */
	CIO_PERMISSION_DENIED = -CIO_SOCKET_ERROR(EACCES),                                          /*!< Permission denied. */
	CIO_PROTOCOL_NOT_SUPPORTED = -CIO_SOCKET_ERROR(EPROTONOSUPPORT),                            /*!< Protocol not supported. */
	CIO_TOO_MANY_FILES_OPEN = -CIO_SOCKET_ERROR(EMFILE),                                        /*!< Too many files open. */
	CIO_TOO_MANY_SYMBOLIC_LINK_LEVELS = -CIO_SOCKET_ERROR(ELOOP),                               /*!< Too many symbolic link levels. */
	CIO_OPERATION_ABORTED = -CIO_WINDOWS_UNIX_SOCKET_ERROR(ERROR_OPERATION_ABORTED, ECANCELED), /*!< Operation cancelled. */
	CIO_NO_MEMORY = -CIO_WINDOWS_UNIX_SOCKET_ERROR(ERROR_OUTOFMEMORY, ENOMEM),                  /*!< Out of memory. */
	CIO_MESSAGE_TOO_LONG = -CIO_SOCKET_ERROR(EMSGSIZE),                                         /*!< Message too long. */
	CIO_NETRESET = -CIO_SOCKET_ERROR(ENETRESET),                                                /*!< Network dropped connection because of reset. */
	CIO_TIMEDOUT = -CIO_SOCKET_ERROR(ETIMEDOUT)                                                 /*!< Timeout while attempting connection. */
};

#ifdef __cplusplus
}
#endif

#endif
