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

#ifndef CIO_WEBSOCKET_MASKING_H
#define CIO_WEBSOCKET_MASKING_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void cio_websocket_correct_mask(uint8_t mask[4], size_t consumed_bytes)
{
	uint_fast8_t array_shift = consumed_bytes % 4;
	uint8_t new_mask[4];
	for (unsigned int i = 0; i < 4; i++) {
		new_mask[i] = mask[(i + array_shift) % 4];
	}

	memcpy(mask, new_mask, 4);
}

static inline void cio_websocket_mask(uint8_t *buffer, size_t length, const uint8_t mask[4])
{
	uint_fast32_t aligned_mask = 0;

	if (length < sizeof(aligned_mask)) {
		for (size_t i = 0; i < length; i++) {
			buffer[i] = buffer[i] ^ (mask[i % 4]);
		}

		return;
	}

	unsigned int pre_length = ((uintptr_t)buffer) % sizeof(aligned_mask);
	pre_length = (sizeof(aligned_mask) - pre_length) % sizeof(aligned_mask);

	size_t main_length = (length - pre_length) / sizeof(aligned_mask);
	unsigned int post_length = (unsigned int)(length - pre_length - (main_length * sizeof(aligned_mask)));

	uint_fast32_t *buffer_aligned = (void *)(buffer + pre_length);

	uint8_t *aligned_mask_filler = (uint8_t *)&aligned_mask;
	for (uint_fast8_t i = 0; i < (uint_fast8_t)sizeof(aligned_mask); i++) {
		*aligned_mask_filler++ = mask[(i + pre_length) % 4];
	}

	unsigned int i_p = 0;
	while (pre_length-- > 0) {
		buffer[i_p] ^= (mask[i_p % 4]);
		i_p++;
	}

	while (main_length-- > 0) {
		*buffer_aligned ^= aligned_mask;
		buffer_aligned++;
	}

	for (size_t i = length - post_length; i < length; i++) {
		buffer[i] ^= (mask[i % 4]);
	}
}

#ifdef __cplusplus
}
#endif

#endif
