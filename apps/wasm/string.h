/* Stand-in for libc's <string.h> on the wasm32-unknown-unknown freestanding target, which ships
 * no libc headers at all. Declares exactly the functions libc_shim.c defines and core/protocol.c
 * (or anything else this build pulls in) actually calls -- add more only when a real link error
 * names one. Found via -Iapps/wasm ahead of anything else named string.h (there is nothing else
 * on this target's search path at all). */
#ifndef DW_WASM_STRING_H
#define DW_WASM_STRING_H
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
size_t strlen(const char *s);

#endif
