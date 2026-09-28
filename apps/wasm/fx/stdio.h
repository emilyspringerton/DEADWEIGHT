/* Stand-in for <stdio.h> -- same "no libc at all" situation math.h documents. fx.c only ever
 * calls snprintf (implemented for real in math_shim.c). sfx.c's sfx_write_wav also references
 * FILE/fopen/fwrite/fclose, but that function is test/offline-dump tooling this build never calls
 * (the browser host drives sfx.c purely through its offline sfx_render() ring-buffer API, never
 * sfx_write_wav) -- these are declared only so sfx.c type-checks; wasm-ld's existing
 * --allow-undefined (apps/wasm's build already relies on this) turns them into unused, never-
 * invoked imports rather than a link error, so no body is needed here. */
#ifndef DW_WASM_STDIO_H
#define DW_WASM_STDIO_H
#include <stddef.h>

int snprintf(char *buf, size_t cap, const char *fmt, ...);

typedef struct FILE FILE;
FILE *fopen(const char *path, const char *mode);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int fclose(FILE *f);

#endif
