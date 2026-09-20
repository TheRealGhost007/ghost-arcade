#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* Small fast xorshift64* PRNG. Deterministic given a seed, no libc rand()
 * global state, no filesystem access (e.g. /dev/urandom) needed per call. */
typedef struct {
    uint64_t state;
} Rng;

void Rng_Seed(Rng *rng, uint64_t seed);
uint64_t Rng_Next(Rng *rng);
/* Returns a uniform value in [0, bound). */
uint32_t Rng_Range(Rng *rng, uint32_t bound);

#endif
