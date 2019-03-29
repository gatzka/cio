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

#ifndef CIO_UTF8_CHECKER_H
#define CIO_UTF8_CHECKER_H

#include <stddef.h>
#include <stdint.h>

#include "cio_export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum cio_utf8_status {
	CIO_UTF8_ACCEPT = 0,
	CIO_UTF8_REJECT = 12
};

struct cio_utf8_state {
	uint8_t codepoint;
	uint8_t state;
};

CIO_EXPORT void cio_utf8_init(struct cio_utf8_state *state);
CIO_EXPORT uint8_t cio_check_utf8(struct cio_utf8_state *state, const uint8_t *s, size_t count);

#ifdef __cplusplus
}
#endif

#endif
