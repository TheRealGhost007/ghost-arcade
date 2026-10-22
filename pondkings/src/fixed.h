#ifndef FIXED_H
#define FIXED_H

#include <stdbool.h>
#include <stdint.h>

/* Pond Kings' simulation runs on whole numbers only: 16.16 fixed point.
 *
 * Online play (a later milestone) will run the same game on two machines
 * from button presses alone. Floating point can round a little differently
 * on two computers or two builds, and one stray rounding is enough to split
 * the two games apart; integers come out the same everywhere. So nothing in
 * game.c or ai.c touches float, and nothing here needs libm. The renderer
 * converts to float only to draw.
 *
 * Shifts are avoided where they would be undefined (a left shift of a
 * negative number): scaling multiplies and divides instead, and C's integer
 * division truncates toward zero on every compiler. */

typedef int32_t fx; /* 16.16 */

#define FX_ONE ((fx)65536)
#define FX_HALF ((fx)32768)
/* A whole number as fx: FX_INT(3). */
#define FX_INT(n) ((fx)((n) * 65536))
/* A fraction as fx: FX_RATIO(3, 4) is 0.75. Exact to the last bit for constants. */
#define FX_RATIO(num, den) ((fx)(((int64_t)(num) * 65536) / (den)))

static inline fx Fx_Mul(fx a, fx b) { return (fx)(((int64_t)a * (int64_t)b) / 65536); }
static inline fx Fx_Div(fx a, fx b) { return b == 0 ? 0 : (fx)(((int64_t)a * 65536) / b); }
static inline fx Fx_Abs(fx a) { return a < 0 ? -a : a; }
static inline fx Fx_Min(fx a, fx b) { return a < b ? a : b; }
static inline fx Fx_Max(fx a, fx b) { return a > b ? a : b; }
static inline fx Fx_Clamp(fx v, fx lo, fx hi) { return v < lo ? lo : (v > hi ? hi : v); }
/* Rendering and logs only: never feed this back into the simulation. */
static inline float Fx_ToFloat(fx a) { return (float)a / 65536.0f; }

/* floor(sqrt(n)) for a 64-bit value, by the bit-by-bit method. */
static inline uint32_t Fx_ISqrt64(uint64_t n) {
    uint64_t res = 0, bit = (uint64_t)1 << 62;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

/* sqrt of an fx value, as fx. */
static inline fx Fx_Sqrt(fx a) { return a <= 0 ? 0 : (fx)Fx_ISqrt64((uint64_t)a * 65536u); }

typedef struct {
    fx x, y;
} V2;

static inline V2 V2_Make(fx x, fx y) { return (V2){x, y}; }
static inline V2 V2_Add(V2 a, V2 b) { return (V2){a.x + b.x, a.y + b.y}; }
static inline V2 V2_Sub(V2 a, V2 b) { return (V2){a.x - b.x, a.y - b.y}; }
static inline V2 V2_Neg(V2 a) { return (V2){-a.x, -a.y}; }
static inline V2 V2_Scale(V2 a, fx s) { return (V2){Fx_Mul(a.x, s), Fx_Mul(a.y, s)}; }
static inline V2 V2_Perp(V2 a) { return (V2){-a.y, a.x}; }
static inline bool V2_IsZero(V2 a) { return a.x == 0 && a.y == 0; }

/* The dot product of two vectors no longer than about 180 units (unit
 * vectors, velocities): the product of two lengths fits in fx. */
static inline fx V2_Dot(V2 a, V2 b) { return Fx_Mul(a.x, b.x) + Fx_Mul(a.y, b.y); }
/* Squared length in 32.32, for comparing distances of any size. */
static inline int64_t V2_Len2(V2 a) { return (int64_t)a.x * a.x + (int64_t)a.y * a.y; }
static inline fx V2_Len(V2 a) { return (fx)Fx_ISqrt64((uint64_t)V2_Len2(a)); }
static inline fx V2_Dist(V2 a, V2 b) { return V2_Len(V2_Sub(a, b)); }
/* Is a within r of b? Exact, with no square root. */
static inline bool V2_Within(V2 a, V2 b, fx r) { return V2_Len2(V2_Sub(a, b)) <= (int64_t)r * r; }

/* A unit vector the same way as a, or zero for a zero (or tiny) vector. */
static inline V2 V2_Norm(V2 a) {
    fx len = V2_Len(a);
    if (len < 16) return (V2){0, 0}; /* under 1/4096 of a unit: no direction to speak of */
    return (V2){Fx_Div(a.x, len), Fx_Div(a.y, len)};
}

/* a, shortened to at most max long. */
static inline V2 V2_ClampLen(V2 a, fx max) {
    if (V2_Len2(a) <= (int64_t)max * max) return a;
    return V2_Scale(V2_Norm(a), max);
}

/* --- the 32 stick directions ---
 * A player's direction crosses the wire (later) as one byte: 0 for none,
 * 1..32 for these, clockwise from right in screen terms (y grows down).
 * Unit vectors to the last bit, made once with Python and pasted, so no
 * machine ever computes a cosine. */
#define DIR_COUNT 32
static const V2 kDirs[DIR_COUNT] = {
    {65536, 0}, {64277, 12785}, {60547, 25080}, {54491, 36410},
    {46341, 46341}, {36410, 54491}, {25080, 60547}, {12785, 64277},
    {0, 65536}, {-12785, 64277}, {-25080, 60547}, {-36410, 54491},
    {-46341, 46341}, {-54491, 36410}, {-60547, 25080}, {-64277, 12785},
    {-65536, 0}, {-64277, -12785}, {-60547, -25080}, {-54491, -36410},
    {-46341, -46341}, {-36410, -54491}, {-25080, -60547}, {-12785, -64277},
    {0, -65536}, {12785, -64277}, {25080, -60547}, {36410, -54491},
    {46341, -46341}, {54491, -36410}, {60547, -25080}, {64277, -12785},
};

/* Direction byte (0 = none) to a unit vector (zero for none or out of range). */
static inline V2 Fx_DirVector(uint8_t dir) { return (dir >= 1 && dir <= DIR_COUNT) ? kDirs[dir - 1] : (V2){0, 0}; }

#endif
