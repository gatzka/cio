/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PCG Random Number Generation for C.
 *
 * Copyright 2014 Melissa O'Neill <oneill@pcg-random.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.
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

#include "cio_compiler.h"
#include "cio_random.h"

static const uint64_t MULTIPLIER = 6364136223846793005ULL;
static const uint_fast8_t FIRST_XOR_SHIFT = 18U;
static const uint_fast8_t SECOND_XOR_SHIFT = 27U;
static const uint_fast8_t ROT_SHIFT = 59U;
static const uint_fast8_t RETURN_SHIFT = 31U;

static void pcg_setseq_64_step_r(struct pcg_state_setseq_64 *rng)
{
	rng->state = rng->state * MULTIPLIER + rng->inc;
}

static void pcg_setseq_64_srandom_r(struct pcg_state_setseq_64 *rng,
									uint64_t initstate, uint64_t initseq)
{
	rng->state = 0U;
	rng->inc = (initseq << 1U) | 1U;
	pcg_setseq_64_step_r(rng);
	rng->state += initstate;
	pcg_setseq_64_step_r(rng);
}

static uint32_t pcg_rotr_32(uint32_t value, unsigned int rot)
{
	return (value >> rot) | (value << ((~rot + 1) & RETURN_SHIFT));
}

static uint32_t pcg_output_xsh_rr_64_32(uint64_t state)
{
	return pcg_rotr_32((uint32_t)(((state >> FIRST_XOR_SHIFT) ^ state) >> SECOND_XOR_SHIFT), (unsigned int)(state >> ROT_SHIFT));
}

static uint32_t pcg32_random_r(cio_rng *rng)
{
	uint64_t oldstate = rng->state;
	pcg_setseq_64_step_r(rng);
	return pcg_output_xsh_rr_64_32(oldstate);

}

void cio_random_seed_rng(cio_rng *rng)
{
	uint64_t seeds[2];
	cio_entropy_get_bytes(&seeds, sizeof(seeds));
	pcg_setseq_64_srandom_r(rng, seeds[0], seeds[1]);
}

void cio_random_get_bytes(cio_rng *rng, void *bytes, size_t num_bytes)
{
	uint8_t *dest = bytes;
	for (size_t i = 0; i < num_bytes; i++) {
		*dest = (uint8_t) pcg32_random_r(rng);
		dest++;
	}
}
