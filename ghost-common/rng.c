#include "rng.h"

void Rng_Seed(Rng *rng, uint64_t seed) {
    /* xorshift64* requires a non-zero seed */
    rng->state = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

uint64_t Rng_Next(Rng *rng) {
    uint64_t x = rng->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

uint32_t Rng_Range(Rng *rng, uint32_t bound) {
    if (bound == 0) return 0;
    /* Lemire's method avoids modulo bias without division-heavy loops. */
    uint64_t r = Rng_Next(rng);
    return (uint32_t)((r & 0xFFFFFFFFu) * bound >> 32);
}
