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

#ifndef CIO_BASE64_H
#define CIO_BASE64_H

#include <stddef.h>
#include <stdint.h>

#include "cio_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Declarations to <a href="https://en.wikipedia.org/wiki/Base64" target="_blank">Base64</a>-encode buffers.
 */

/**
 * @brief <a href="https://en.wikipedia.org/wiki/Base64" target="_blank">Base64</a>-encodes a buffer
 *
 * @param in The buffer that should be encoded.
 * @param in_length The length of the input buffer to be encoded.
 * @param out A pointer to a buffer containing the Base64-encoded string.
 *
 * @warning The memory @p in and @p out points to must not overlap.
 */
CIO_EXPORT void cio_b64_encode_buffer(const uint8_t *__restrict in, size_t in_length, char *__restrict out);

#ifdef __cplusplus
}
#endif

#endif
