#!/usr/bin/env bash
# build_wasm_native.sh -- real, native wasm32 build of DEADWEIGHT's wire-protocol codec (no
# Emscripten). Founder real-time, 2026-09-28: "do not use emscripten it doesnt work it needs to
# be native wasm like mixforge" -- MIXFORGE's own web/dsp.wasm / web/room.wasm are PARENA source
# through `parena build` -> LLVM IR -> `llc -mtriple=wasm32-unknown-unknown` -> `wasm-ld`.
# DEADWEIGHT's client logic (apps/wasm/, core/protocol.c) is hand-written C, not PARENA source,
# so the honest equivalent is the same backend one step earlier: plain
# `clang -target wasm32-unknown-unknown -nostdlib` straight from that C, linked with the same
# `wasm-ld` -- zero Emscripten SDK, zero generated JS runtime, zero libc emulation. See
# docs/NATIVE_WASM_CLIENT_NORTHSTAR.md for the full account (including the Emscripten attempt
# this replaced).
#
# Finds clang/wasm-ld under LLVM_TOOLCHAIN_ROOT (MIXFORGE's own no-sudo-extracted LLVM-18 tree,
# see PARENA/docs/LLVM_BACKEND_NORTHSTAR.md) if present, else on PATH.
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
  echo "build_wasm_native.sh: clang/wasm-ld not found (LLVM_TOOLCHAIN_ROOT or PATH) -- see PARENA/docs/LLVM_BACKEND_NORTHSTAR.md for the no-sudo apt-get download + dpkg-deb -x recipe" >&2
  exit 1
fi

OUT_DIR="web-wasm/generated"
mkdir -p "$OUT_DIR"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

SRCS="apps/wasm/libc_shim.c apps/wasm/protocol_wasm.c core/protocol.c"
OBJS=""
for src in $SRCS; do
  base="$(basename "$src" .c)"
  "$CLANG" -target wasm32-unknown-unknown -nostdlib -O2 -Iapps/wasm -Icore -c "$src" -o "$tmp/$base.o"
  OBJS="$OBJS $tmp/$base.o"
done
"$WASM_LD" --no-entry --export-all --allow-undefined -o "$OUT_DIR/dw_protocol.wasm" $OBJS

node tests/test_wasm_protocol.mjs "$OUT_DIR/dw_protocol.wasm"
echo "build_wasm_native.sh: $OUT_DIR/dw_protocol.wasm built and verified (native wasm32, no Emscripten)"
