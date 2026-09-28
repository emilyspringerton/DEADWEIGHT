#!/usr/bin/env bash
# build_wasm_fx.sh -- real, native wasm32 build of DEADWEIGHT's round-resolution animation/audio
# engine (apps/gui/fx.c + apps/gui/sfx.c, compiled completely unmodified). Same backend as
# scripts/build_wasm_native.sh (plain `clang -target wasm32-unknown-unknown -nostdlib`, no
# Emscripten) -- see that script's own header comment and docs/NATIVE_WASM_CLIENT_NORTHSTAR.md.
#
# Founder real-time, 2026-09-28: "write an sdl layer for wasm for when we dont have SDL abstract
# our shit so we can use PARENA to write the same exact logic" -- apps/wasm/fx/SDL.h is that
# layer: fx.c's entire real SDL footprint is five draw primitives (setc/frect/fline/fcircle/fring/
# fpoly all funnel through them), declared there and implemented as wasm imports the browser host
# (web/src/fxWasm.ts) fills by drawing to Canvas2D -- Windows fills the identical five calls via
# real SDL2 (apps/gui/main.c). Same C source, two hosts, not a second hand-written reimplementation
# of fx.c's own animation/audio logic in TypeScript.
#
# apps/wasm/fx/{math,stdio,stdlib}.h + math_shim.c cover the small, closed set of libc fx.c/sfx.c
# actually call that apps/wasm's existing string.h/libc_shim.c don't already (transcendentals +
# snprintf) -- see tests/test_wasm_math_shim.c for their own accuracy check against a real libm,
# run below before this ever builds the wasm module itself.
#
# sfx.c is compiled with -DDW_SFX_NO_SDL, which compiles its real SDL audio-device path out
# entirely (apps/gui/sfx.c's own #ifndef DW_SFX_NO_SDL guards) -- audio reaches the browser through
# sfx.c's own pre-existing offline-render API instead (sfx_offline_begin/sfx_render), the same one
# tests/test_sfx.c and `dw_gui --fx-demo` already use to dump WAV without a sound card.
set -euo pipefail
cd "$(dirname "$0")/.."

LLVM_TOOLCHAIN_ROOT="${LLVM_TOOLCHAIN_ROOT:-$HOME/.local/opt/llvm-toolchain}"
if [ -x "$LLVM_TOOLCHAIN_ROOT/usr/lib/llvm-18/bin/clang" ]; then
  CLANG="$LLVM_TOOLCHAIN_ROOT/usr/lib/llvm-18/bin/clang"
  WASM_LD="$LLVM_TOOLCHAIN_ROOT/usr/lib/llvm-18/bin/wasm-ld"
  export LD_LIBRARY_PATH="$LLVM_TOOLCHAIN_ROOT/usr/lib/x86_64-linux-gnu:$LLVM_TOOLCHAIN_ROOT/usr/lib/llvm-18/lib:${LD_LIBRARY_PATH:-}"
else
  CLANG="$(command -v clang || true)"
  WASM_LD="$(command -v wasm-ld || true)"
fi
if [ -z "$CLANG" ] || [ -z "$WASM_LD" ]; then
  echo "build_wasm_fx.sh: clang/wasm-ld not found (LLVM_TOOLCHAIN_ROOT or PATH) -- see PARENA/docs/LLVM_BACKEND_NORTHSTAR.md" >&2
  exit 1
fi

echo "== math shim accuracy check (native, against a real libm) =="
mkdir -p build
GCC="${GCC:-gcc}"
"$GCC" -std=c99 -Wall -Wextra -Werror -O2 \
  -Dsinf=dw_shim_sinf -Dcosf=dw_shim_cosf -Dexpf=dw_shim_expf -Dexp2f=dw_shim_exp2f -Dlogf=dw_shim_logf -Dfmodf=dw_shim_fmodf -Dpowf=dw_shim_powf -Dtanhf=dw_shim_tanhf -Dsnprintf=dw_shim_snprintf \
  -c apps/wasm/fx/math_shim.c -o build/math_shim_renamed.o -Iapps/wasm/fx
"$GCC" -std=c99 -Wall -Wextra -Werror -O2 tests/test_wasm_math_shim.c build/math_shim_renamed.o -o build/test_wasm_math_shim -lm
./build/test_wasm_math_shim

OUT_DIR="web/dist/generated"
mkdir -p "$OUT_DIR"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

INCLUDES="-Iapps/wasm/fx -Iapps/wasm -Icore -Iapps/gui"
# -Wno-parentheses-equality: PARENA's C emitter always parenthesizes generated `if` conditions
# ("(scenario == 10)"), which clang (not gcc -- the native build's own compiler) flags; cosmetic,
# and this is generated code ("do not edit by hand") so it's silenced, not hand-fixed.
CFLAGS="-target wasm32-unknown-unknown -nostdlib -O2 -Wall -Wextra -Werror -Wno-parentheses-equality $INCLUDES"
SRCS="apps/wasm/libc_shim.c apps/wasm/fx/math_shim.c apps/wasm/fx/fx_wasm.c core/card_rules.c core/card_text.c apps/gui/fx.c"
OBJS=""
for src in $SRCS; do
  base="$(basename "$src" .c)"
  "$CLANG" $CFLAGS -c "$src" -o "$tmp/$base.o"
  OBJS="$OBJS $tmp/$base.o"
done
# core/fx_rules.c calls card_kind/kind_beats (core/card_rules.h) without including that header
# itself -- the real native build (scripts/build.sh) already needs the identical `-include
# card_rules.h` forced-include to compile this same generated file, not a wasm-specific quirk.
"$CLANG" $CFLAGS -include card_rules.h -c core/fx_rules.c -o "$tmp/fx_rules.o"
OBJS="$OBJS $tmp/fx_rules.o"
"$CLANG" $CFLAGS -DDW_SFX_NO_SDL -c apps/gui/sfx.c -o "$tmp/sfx.o"
OBJS="$OBJS $tmp/sfx.o"

"$WASM_LD" --no-entry --export-all --allow-undefined -o "$OUT_DIR/dw_fx.wasm" $OBJS
echo "build_wasm_fx.sh: $OUT_DIR/dw_fx.wasm built (native wasm32, no Emscripten)"
