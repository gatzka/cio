/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PCG Random Number Generation for C.
 *
 * Copyright 2014 Melissa O'Neill <oneill@pcg-random.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For additional information about the PCG random number generation scheme,
 * including its license and other licensing options, visit
 *
 *     http://www.pcg-random.org
 */

#include <stdint.h>

#include "cio_random.h"

struct pcg_state_setseq_64 {
	uint64_t state; // RNG state. All values are possible.
	uint64_t inc; // Controls which RNG sequence (stream) is selected. Must *always* be odd.
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
