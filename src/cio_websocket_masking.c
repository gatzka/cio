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

#include <stddef.h>
#include <stdint.h>

#include "cio_websocket_masking.h"

void cio_websocket_mask(uint8_t *buffer, size_t length, const uint8_t mask[4])
{
	uint_fast32_t aligned_mask;
	if (length < sizeof(aligned_mask)) {
		for (size_t i = 0; i < length; i++) {
			buffer[i] = buffer[i] ^ (mask[i % 4]);
		}

		return;
	}

	unsigned int pre_length = ((uintptr_t) buffer) % sizeof(aligned_mask);
	pre_length = sizeof(aligned_mask) - pre_length;
	size_t main_length = (length - pre_length) / sizeof(aligned_mask);
	unsigned int post_length = length - pre_length - (main_length * sizeof(aligned_mask));
	uint_fast32_t *buffer_aligned = (void *)(buffer + pre_length);

	aligned_mask = 0x00;
	for (unsigned int i = 0; i < sizeof(aligned_mask); i++) {
		aligned_mask |= ((uint_fast32_t)mask[(i + pre_length) % 4]) << (i * 8);
	}

	for (unsigned int i = 0; i < pre_length; i++) {
		buffer[i] ^= (mask[i % 4]);
	}

	for (size_t i = 0; i < main_length; i++) {
		*buffer_aligned ^= aligned_mask;
		buffer_aligned++;
	}

	for (size_t i = length - post_length; i < length; i++) {
		buffer[i] ^= (mask[i % 4]);
	}
}
