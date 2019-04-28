/*
 * SPDX-License-Identifier: MIT
 *
 *The MIT License (MIT)
 *
 * Copyright (c) <2019> <Stephan Gatzka>
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

#include <stdint.h>

#include "cio_random.h"

struct pcg_state_setseq_64 {    // Internals are *Private*.
	uint64_t state;             // RNG state.  All values are possible.
	uint64_t inc;               // Controls which RNG sequence (stream) is selected. Must *always* be odd.
};

typedef struct pcg_state_setseq_64 pcg32_random_t;


static const uint64_t MULTIPLIER = 6364136223846793005ULL;
static const unsigned int FIRST_XOR_SHIFT = 18U;
static const unsigned int SECOND_XOR_SHIFT = 27U;
static const unsigned int ROT_SHIFT = 59U;
static const unsigned int RETURN_SHIFT = 31U;

uint32_t pcg32_random_r(pcg32_random_t* rng);
uint32_t pcg32_random_r(pcg32_random_t* rng)
{
	uint64_t oldstate = rng->state;
	rng->state = oldstate * MULTIPLIER + rng->inc;
	uint32_t xorshifted = (uint32_t)((oldstate >> FIRST_XOR_SHIFT) ^ oldstate) >> SECOND_XOR_SHIFT;
	uint32_t rot = (uint32_t)(oldstate >> ROT_SHIFT);
	return (xorshifted >> rot) | (xorshifted << ((-rot) & RETURN_SHIFT));
}
