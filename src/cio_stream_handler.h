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

#ifndef CIO_STREAM_HANDLER_H
#define CIO_STREAM_HANDLER_H

#include <stdint.h>

#include "cio_error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Definition of the stream callback function type.
 * need to know.
 */

/**
 * @brief The type of a function passed to all stream callback functions.
 * 
 * @param handler_context The context the functions works on.
 * @param err If err != ::cio_success, the read failed.
 * @param buf A pointer to the begin of the buffer where the data was read in. 
 * @param bytes_transferred The number of bytes transferred into @p buf.
 */
typedef void (*cio_stream_read_handler)(void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred);
typedef void (*cio_stream_write_handler)(void *handler_context, enum cio_error err, size_t bytes_transferred);

#ifdef __cplusplus
}
#endif

#endif
