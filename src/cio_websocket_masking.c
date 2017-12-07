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

void cio_websocket_mask(uint8_t *buffer, size_t length, const uint8_t mask[4], size_t bytewidth)
{
	if (length < 8) {
		bytewidth = 1;
	}

	size_t shift = 1;
	if (bytewidth > 2) {
		shift = 2;
	}

	if (bytewidth > 4) {
		shift = 3;
	}

	size_t pre_length, main_length, post_length;
	void *ptr_aligned;
	uint32_t mask32;

	switch (bytewidth) {
	case 8:
		pre_length = ((size_t) buffer) % bytewidth;
		pre_length = bytewidth - pre_length;
		main_length = (length - pre_length) >> shift;
		post_length = length - pre_length - (main_length << shift);
		ptr_aligned = buffer + pre_length;

		mask32 = 0x0;
		for (unsigned int i = 0; i < 4; i++) {
			mask32 |= (((uint32_t) *(mask + (i + pre_length) % 4)) & 0xFF) << (i * 8);
		}

		for (size_t i = 0; i < pre_length; i++) {
			buffer[i] ^= (mask[i % 4]);
		}

		uint64_t mask64 = ((uint64_t) mask32) & 0xFFFFFFFF;
		mask64 |= (mask64 << 32) & 0xFFFFFFFF00000000;
		uint64_t *buffer_aligned64 = ptr_aligned;
		for (size_t i = 0; i < main_length; i++) {
			buffer_aligned64[i] ^= mask64;
		}

		for (size_t i = length - post_length; i < length; i++) {
			buffer[i] ^= (mask[i % 4]);
		}

		break;

	case 4:
		pre_length = ((size_t) buffer) % bytewidth;
		pre_length = bytewidth - pre_length;
		main_length = (length - pre_length) >> shift;
		post_length = length - pre_length - (main_length << shift);
		ptr_aligned = buffer + pre_length;

		mask32 = 0x0;
		for (unsigned int i = 0; i < 4; i++) {
			mask32 |= (((uint32_t) *(mask + (i + pre_length) % 4)) & 0xFF) << (i * 8);
		}

		for (size_t i = 0; i < pre_length; i++) {
			buffer[i] ^= (mask[i % 4]);
		}

		uint32_t *buffer_aligned32 = ptr_aligned;
		for (size_t i = 0; i < main_length; i++) {
			buffer_aligned32[i] ^= mask32;
		}

		for (size_t i = length - post_length; i < length; i++) {
			buffer[i] ^= (mask[i % 4]);
		}

		break;

	default:
		for (size_t i = 0; i < length; i++) {
			buffer[i] = buffer[i] ^ (mask[i % 4]);
		}

		break;
	}
}
