/* Numeric accuracy check for apps/wasm/fx/math_shim.c's sinf/cosf/expf/logf/fmodf/powf against a
 * real libm -- compiled and run NATIVELY (not wasm), so this can link a real <math.h> for the
 * comparison. The shim's own global symbols are renamed at compile time (see the -D flags this is
 * built with, scripts/build_wasm_fx.sh) so they don't collide with libm's real ones in this one
 * binary. Every call site in fx.c/sfx.c is cosmetic (shake decay, shimmer phase, pitch/volume
 * curves) -- this asserts an error bound generous enough for that, not bit-for-bit libm parity. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern float dw_shim_sinf(float);
extern float dw_shim_cosf(float);
extern float dw_shim_expf(float);
extern float dw_shim_logf(float);
extern float dw_shim_fmodf(float, float);
extern float dw_shim_powf(float, float);
extern float dw_shim_tanhf(float);
extern float dw_shim_exp2f(float);

static int checks = 0, failures = 0;

static void check(const char *name, float got, float want, float tol) {
    checks++;
    float err = fabsf(got - want);
    if (err > tol) {
        failures++;
        fprintf(stderr, "FAIL %s: got=%.6f want=%.6f err=%.6f (tol %.6f)\n", name, got, want, err, tol);
    }
}

int main(void) {
    /* sin/cos across the full reduced range plus values well outside [-pi,pi], since real call
     * sites (rot = t*0.006f with t up to ~9000ms) exceed it and rely on range reduction. */
    for (float x = -30.0f; x <= 30.0f; x += 0.037f) {
        check("sinf", dw_shim_sinf(x), sinf(x), 2e-3f);
        check("cosf", dw_shim_cosf(x), cosf(x), 2e-3f);
    }
    /* expf: real call sites are expf(-dt*7.0f) with dt in seconds, so a small negative range, but
     * check a wider span since the implementation is general. */
    for (float x = -10.0f; x <= 4.0f; x += 0.05f) check("expf", dw_shim_expf(x), expf(x), fabsf(expf(x)) * 0.01f + 1e-4f);
    /* logf/powf: real call sites always pass a positive base (2.0, 1.2, a frequency ratio). */
    for (float x = 0.01f; x <= 20.0f; x += 0.03f) check("logf", dw_shim_logf(x), logf(x), fabsf(logf(x)) * 0.01f + 1e-3f);
    for (float b = 0.5f; b <= 4.0f; b += 0.1f)
        for (float e = -3.0f; e <= 3.0f; e += 0.5f)
            check("powf", dw_shim_powf(b, e), powf(b, e), fabsf(powf(b, e)) * 0.02f + 1e-3f);
    /* fmodf: real call sites are fmodf(x, 1.0f) with x >= 0, but check a general span too. */
    for (float x = -20.0f; x <= 20.0f; x += 0.07f)
        for (float y = 0.3f; y <= 3.0f; y += 0.7f)
            check("fmodf", dw_shim_fmodf(x, y), fmodf(x, y), 1e-3f);

    for (float x = -8.0f; x <= 8.0f; x += 0.03f) check("tanhf", dw_shim_tanhf(x), tanhf(x), 2e-3f);
    for (float x = -10.0f; x <= 4.0f; x += 0.05f) check("exp2f", dw_shim_exp2f(x), exp2f(x), fabsf(exp2f(x)) * 0.01f + 1e-4f);

    printf("test_wasm_math_shim: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
