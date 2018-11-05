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

#ifndef CIO_UTIL_H
#define CIO_UTIL_H

#include <stddef.h>

/**
 * @file
 * @brief Utility functions.
 */

/**
 * @hideinitializer
 * Get the pointer to a structure encapsulating a structure to which @p ptr points to.
 */
#define cio_container_of(ptr, type, member) ( \
    (void *)((char *)(ptr)-offsetof(type, member)))

/**
 * @hideinitializer
 * Get a @p const pointer to a structure encapsulating a structure to which @p ptr points to.
 */
#define cio_const_container_of(ptr, type, member) ( \
    (const void *)((const char *)(ptr)-offsetof(type, member)))

#endif
