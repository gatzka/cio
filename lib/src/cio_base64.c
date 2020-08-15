/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2016> <Stephan Gatzka>
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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cio/base64.h"
#include "cio/compiler.h"

static const char ENCODE_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void cio_b64_encode_buffer(const uint8_t *__restrict in, size_t in_length, char *__restrict out)
{
	while (in_length) {
		unsigned int len = 0;
		unsigned int triple[3];
		for (unsigned int i = 0; i < 3; i++) {
			if (in_length) {
				triple[i] = *in++;
				len++;
				in_length--;
			} else {
				triple[i] = 0;
			}
		}

		char tmp[4];
		tmp[0] = ENCODE_TABLE[triple[0] >> 2U];
		tmp[1] = ENCODE_TABLE[((triple[0] & 0x03U) << 4U) | ((triple[1] & 0xf0U) >> 4U)]; // NOLINT
		if (cio_likely(len > 1)) {
			tmp[2] = ENCODE_TABLE[((triple[1] & 0x0fU) << 2U) | ((triple[2] & 0xc0U) >> 6U)]; // NOLINT
		} else {
			tmp[2] = '=';
		}
		if (cio_likely(len > 2)) {
			tmp[3] = ENCODE_TABLE[triple[2] & 0x3fU]; // NOLINT
		} else {
			tmp[3] = '=';
		}
		memcpy(out, tmp, sizeof(tmp));
		out += 4;
	}
}
