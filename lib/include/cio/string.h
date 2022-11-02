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

#ifndef CIO_STRING_H
#define CIO_STRING_H

#include <stddef.h>

#include "cio/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Wrapper functions for non-ANSI-C conformant string functions.
 */

/**
 * @brief Finds the start of the first occurence of @p needle in @p haystack.
 * @param haystack The memory to be searched.
 * @param haystacklen The length of memory to be searched.
 * @param needle The substring that should be searched for.
 * @param needlelen The length of the substring to be searched for.
 * @return Pointer to the beginning of the substring, @c NULL otherwise.
 */
CIO_EXPORT const void *cio_memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);

/**
 * @brief Compare two strings ignoring case.
 * @param string1 First string to compare.
 * @param string2 Second string to compare.
 * @param max_number_of_bytes Maximum number of bytes to be compared.
 * @return an integer less than, equal to, or greater than zero if @p string1 is, after ignoring case,
 * found to be less than, to match, or be greater than @p string2, respectively
 */
CIO_EXPORT int cio_strncasecmp(const char *string1, const char *string2, size_t max_number_of_bytes);

#ifdef __cplusplus
}
#endif

#endif
