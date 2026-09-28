/* Stand-in for PARENA's real, general-purpose C runtime (core/runtime/parena_runtime.h, 3240
 * lines: Vec/String helpers, sockets, SDL2, mbedTLS, ptys...) used ONLY when compiling
 * core/fx_rules.c and core/card_rules.c for the wasm32-unknown-unknown freestanding target.
 * PARENA's C emitter always prepends #include "parena_runtime.h" to every generated file as a
 * fixed template, regardless of whether that file's own generated body actually calls anything
 * from it (there is no scalar-only/no-runtime emission mode -- checked directly against
 * PARENA/src/emit.c) -- and the real runtime is unbuildable freestanding (real POSIX/SDL2/
 * mbedTLS headers, no freestanding equivalent exists for any of them). fx_rules.c/card_rules.c's
 * own generated bodies call nothing from it (checked directly: zero libc/runtime identifiers in
 * either file, confirmed empty here by iterative compilation -- add something only when a real
 * compile error names it, never speculatively, same policy apps/wasm/string.h already states). */
#ifndef DW_WASM_PARENA_RUNTIME_H
#define DW_WASM_PARENA_RUNTIME_H
#endif
