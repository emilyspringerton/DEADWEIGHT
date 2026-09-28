/* Stand-in for <stdlib.h> -- fx.c and sfx.c both include it but (checked directly, grepped) call
 * nothing from it: no malloc/free/rand/atoi/qsort anywhere in either file. An empty header (this
 * file) is the honest shim -- adding stubs for functions nothing calls would be speculative, which
 * apps/wasm/string.h's own header comment already rules out as a policy for this build. */
#ifndef DW_WASM_STDLIB_H
#define DW_WASM_STDLIB_H
#endif
