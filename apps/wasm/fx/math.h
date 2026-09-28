/* Stand-in for <math.h> on the wasm32-unknown-unknown freestanding target -- there is no libm
 * here, same "no libc at all, not a smaller one" situation apps/wasm/string.h already documents
 * for <string.h>. sqrtf/fabsf/sqrt need no real implementation: they're wasm MVP native
 * instructions (f32.sqrt, f32.abs, f64.sqrt), so clang's own __builtin_* forms compile straight to
 * one instruction with zero call overhead -- declared static inline right here, not in
 * math_shim.c. sinf/cosf/expf/logf/fmodf/powf have no native instruction and need a real (if
 * approximate) software implementation -- see math_shim.c. These are used only for cosmetic
 * animation/audio math (shake decay, shimmer phase, pitch/volume curves), never gameplay-affecting
 * logic, so a few units in the last decimal place of error is an explicit, acceptable tradeoff --
 * verified numerically against a real libm in tests/test_wasm_math_shim.c before this shipped. */
#ifndef DW_WASM_MATH_H
#define DW_WASM_MATH_H

static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline float fminf(float a, float b) { return a < b ? a : b; }
static inline float fmaxf(float a, float b) { return a > b ? a : b; }

float sinf(float x);
float cosf(float x);
float expf(float x);
float exp2f(float x);
float logf(float x);
float fmodf(float x, float y);
float powf(float base, float exp);
float tanhf(float x);

#endif
