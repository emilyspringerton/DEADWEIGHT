/* Real (approximate, not table/libm-backed) implementations of the handful of transcendental
 * <math.h> functions apps/gui/fx.c and apps/gui/sfx.c actually call that don't map to a native
 * wasm instruction (see math.h's own header comment for sqrtf/fabsf/sqrt, which do and need no
 * implementation here). Classic, widely-used minimax/range-reduction techniques, not invented on
 * the spot -- accuracy is verified against a real libm in tests/test_wasm_math_shim.c. Every use
 * site in fx.c/sfx.c is cosmetic (shake decay, shimmer phase, pitch/volume curves), never
 * gameplay-affecting, so this bar (a few units of last-place error) is the right one -- matching
 * the same "structurally correct, not bit-identical" allowance already established for the
 * browser's Canvas2D/Web Audio rendering itself. */
#include "math.h"

/* sin/cos: reduce to the nearest multiple of pi/2 (quadrant k) plus a remainder y in
 * [-pi/4, pi/4], then a plain Taylor series for sin(y)/cos(y) -- degree 7 and 6 respectively --
 * which converges to ~1e-7 over that narrow a range (no minimax table needed, no accuracy cliff
 * near the ends of a wider reduction interval). sin(x)/cos(x) then follow from y and k mod 4 via
 * the standard quadrant identities. Safe for every real call site in this codebase (the largest
 * argument is a few thousand ms times a sub-1 coefficient, nowhere near i32 range). */
static const float HALF_PI_F = 1.57079632679489661923f;

static void reduce_quadrant(float x, float *y, int *q) {
    float k = (float)(int)(x / HALF_PI_F + (x >= 0.0f ? 0.5f : -0.5f)); /* round to nearest int */
    *y = x - k * HALF_PI_F;
    int qi = (int)k % 4;
    if (qi < 0) qi += 4;
    *q = qi;
}

static float sin_taylor(float y) { /* accurate for |y| <= pi/4 */
    float y2 = y * y;
    return y * (1.0f + y2 * (-1.0f / 6.0f + y2 * (1.0f / 120.0f + y2 * (-1.0f / 5040.0f))));
}
static float cos_taylor(float y) { /* accurate for |y| <= pi/4 */
    float y2 = y * y;
    return 1.0f + y2 * (-0.5f + y2 * (1.0f / 24.0f + y2 * (-1.0f / 720.0f)));
}

float sinf(float x) {
    float y; int q; reduce_quadrant(x, &y, &q);
    switch (q) {
    case 0: return sin_taylor(y);
    case 1: return cos_taylor(y);
    case 2: return -sin_taylor(y);
    default: return -cos_taylor(y);
    }
}

float cosf(float x) {
    float y; int q; reduce_quadrant(x, &y, &q);
    switch (q) {
    case 0: return cos_taylor(y);
    case 1: return -sin_taylor(y);
    case 2: return -cos_taylor(y);
    default: return sin_taylor(y);
    }
}

/* fmodf: truncating remainder, matching C's fmod sign convention. Every call site here passes a
 * non-negative x and y=1.0, so the general form is more than sufficient. */
float fmodf(float x, float y) {
    if (y == 0.0f) return 0.0f;
    float n = (float)(int)(x / y);
    return x - n * y;
}

/* expf: classic range-reduce-by-ln2 + polynomial + bit-manipulated 2^n scale. Clamped against the
 * (never actually hit, given real call sites) overflow/underflow edges rather than left undefined. */
float expf(float x) {
    if (x > 80.0f) return 3.0e38f;
    if (x < -80.0f) return 0.0f;
    const float LOG2E = 1.4426950408889634f;
    const float LN2 = 0.6931471805599453f;
    float fx = x * LOG2E;
    int n = (int)(fx >= 0.0f ? fx + 0.5f : fx - 0.5f);
    float r = x - (float)n * LN2;
    float r2 = r * r;
    float p = 1.0f + r + r2 * (0.5f + r * (0.16666667f + r * (0.041666667f + r * 0.0083333333f)));
    union { float f; int i; } u;
    u.f = 1.0f;
    u.i += (n << 23);
    return p * u.f;
}

/* logf: bit-decompose into mantissa [1,2) x 2^e, then ln(m) = 2*atanh((m-1)/(m+1)) via its own
 * odd-power series, plus e*ln2. Only ever called (via powf below) with a strictly positive base in
 * this codebase (2.0, 1.2, a frequency ratio > 0), so the x<=0 branch is a defensive fallback, not
 * a real code path. */
float logf(float x) {
    if (x <= 0.0f) return -3.0e38f;
    union { float f; int i; } u;
    u.f = x;
    int e = ((u.i >> 23) & 0xFF) - 127;
    u.i = (u.i & 0x007FFFFF) | 0x3F800000;
    float m = u.f;
    float t = (m - 1.0f) / (m + 1.0f);
    float t2 = t * t;
    float p = t * (2.0f + t2 * (0.6666667f + t2 * (0.4f + t2 * 0.2857143f)));
    return p + (float)e * 0.6931471805599453f;
}

/* powf: general form via expf(exp * logf(base)) -- every real call site here (mtof's semitone
 * table, fx.c's combo-escalation 1.2^steps, sfx.c's pitch-glide frequency ratio) has a strictly
 * positive base, so this covers the actual usage exactly; base<=0 is not a real path. */
float powf(float base, float exp) {
    if (base <= 0.0f) return 0.0f;
    return expf(exp * logf(base));
}

/* exp2f: never called directly anywhere in fx.c/sfx.c/this file's own source (checked) -- LLVM's
 * backend introduces this call on its own, canonicalizing powf's expf(exp*logf(base)) pattern
 * into an exp2f-based form since it assumes a standard libm exists wherever a function literally
 * named "expf"/"powf"/etc. is called. Found only at the wasm-ld link stage (an unresolved-symbol
 * import LLVM added silently, not a compile-time call this file's own source shows) -- implemented
 * here, not routed around, since the alternative (renaming our own expf/powf to avoid the
 * compiler's name-based recognition) would be a much stranger, more fragile shim. */
float exp2f(float x) { return expf(x * 0.6931471805599453f); }

/* tanhf: via expf, matching sfx.c's own soft-clip/drive usage (bounded output, no precision need
 * beyond "sounds right"). */
float tanhf(float x) {
    if (x > 20.0f) return 1.0f;
    if (x < -20.0f) return -1.0f;
    float e2x = expf(2.0f * x);
    return (e2x - 1.0f) / (e2x + 1.0f);
}

/* snprintf: fx.c only ever formats "%d", "%s" and literal characters (damage/armor/energy pop-up
 * labels, the scenario name string) -- see apps/gui/fx.c's own snprintf call sites. This covers
 * exactly that subset, truncating to fit like the real snprintf, not a general-purpose clone. */
static int fmt_uint(char *buf, int cap, int pos, unsigned v) {
    char digits[12]; int n = 0;
    if (v == 0) digits[n++] = '0';
    while (v > 0) { digits[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = n - 1; i >= 0; i--) { if (pos < cap) buf[pos] = digits[i]; pos++; }
    return pos;
}

int snprintf(char *buf, unsigned long cap, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int pos = 0;
    int icap = cap > 0 ? (int)cap - 1 : 0; /* reserve room for the trailing NUL */
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { if (pos < icap) buf[pos] = *p; pos++; continue; }
        p++;
        if (*p == 'd') {
            int v = __builtin_va_arg(ap, int);
            if (v < 0) { if (pos < icap) buf[pos] = '-'; pos++; v = -v; }
            pos = fmt_uint(buf, icap, pos, (unsigned)v);
        } else if (*p == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            while (s && *s) { if (pos < icap) buf[pos] = *s; pos++; s++; }
        } else if (*p == '%') {
            if (pos < icap) buf[pos] = '%';
            pos++;
        } else if (*p == 0) {
            break;
        } else {
            if (pos < icap) buf[pos] = *p;
            pos++;
        }
    }
    __builtin_va_end(ap);
    if (cap > 0) buf[pos < icap ? pos : icap] = 0;
    return pos;
}
