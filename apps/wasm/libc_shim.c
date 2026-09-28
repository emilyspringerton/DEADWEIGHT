/* Freestanding libc shim for the wasm32-unknown-unknown build (apps/wasm). There is no libc for
 * this target at all -- not a smaller one, none -- so anything core's C files call from
 * string.h has to be provided here (string.h itself is stubbed in apps/wasm/string.h since
 * there's no real one to declare against). Kept to exactly what core/protocol.c (and whatever
 * else this build
 * pulls in) actually calls; add more only when a real link error names one, never speculatively.
 * See docs/NATIVE_WASM_CLIENT_NORTHSTAR.md for why this exists instead of Emscripten (founder
 * real-time, 2026-09-28: "do not use emscripten it doesnt work it needs to be native wasm like
 * mixforge" -- MIXFORGE's own web/dsp.wasm / web/room.wasm precedent is PARENA source through
 * `parena build` -> LLVM IR -> `llc -mtriple=wasm32-unknown-unknown` -> `wasm-ld`; this repo's
 * client logic is hand-written C, not PARENA source, so the equivalent honest pipeline is plain
 * `clang -target wasm32-unknown-unknown -nostdlib` straight from that C -- same backend, same
 * "no Emscripten SDK, no JS runtime shim, no libc emulation" result, real freestanding wasm32). */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
