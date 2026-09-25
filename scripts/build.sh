#!/usr/bin/env bash
# Clean build + test. Usage: scripts/build.sh [--windows] [--android] [--gui] [--all]
# --gui needs libsdl2-dev (pkg-config sdl2); it is NOT part of --all so machines without SDL stay green. With
# --gui --windows the Windows dw_gui.exe is also cross-built if SDL2_MINGW (or ./sdl2_mingw) points at the SDL2 mingw dev tree.
# Default: C core, ASan+UBSan tests, server+client binaries. Anything that fails stops the script (set -e).
set -euo pipefail
cd "$(dirname "$0")/.."
BAZEL="${BAZEL:-bazel}"
VERSION="${DW_VERSION:-0.0.0-dev}"
CFLAGS_BASE="-std=c99 -Wall -Wextra -Werror -Icore -Icore/runtime -DPARENA_NO_GRAPHICS -DDW_VERSION=\"$VERSION\""
rm -rf build && mkdir -p build

echo "== C: rules tests (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    tests/test_rules.c core/card_rules.c -o build/test_rules
./build/test_rules tests/parity_vectors.txt

echo "== C: fx (round-resolution animation/audio decision layer) rules tests (ASan+UBSan) =="
gcc $CFLAGS_BASE -include card_rules.h -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    tests/test_fx_rules.c core/card_rules.c core/fx_rules.c -o build/test_fx_rules
./build/test_fx_rules

echo "== C: protocol codec tests (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    tests/test_protocol.c core/protocol.c -o build/test_protocol
./build/test_protocol

echo "== C: match core tests (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    tests/test_match.c core/match.c core/card_rules.c -o build/test_match
./build/test_match

echo "== C: draft + deck-match tests (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    tests/test_draft.c core/draft.c core/match.c core/card_rules.c -o build/test_draft
./build/test_draft

echo "== C: synthesized sound design tests (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all -DDW_SFX_NO_SDL \
    tests/test_sfx.c apps/gui/sfx.c -lm -o build/test_sfx
./build/test_sfx

echo "== C: bot brain tests (ASan+UBSan) =="
BRAIN_SRC="core/brain.c core/bot_brain.c core/brain_default.c core/runtime/parena_runtime.c"
gcc $CFLAGS_BASE -include card_rules.h -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    tests/test_brain.c core/policy.c core/card_rules.c $BRAIN_SRC -lm -o build/test_brain
./build/test_brain
gcc $CFLAGS_BASE -include card_rules.h -O1 tests/brain_dump.c core/policy.c core/card_rules.c $BRAIN_SRC -lm -o build/brain_dump
if command -v python3 >/dev/null; then (cd training && python3 -m unittest test_dw_brain -v 2>&1 | tail -4); fi

echo "== C: server + client =="
SERVER_SRC="apps/server/main.c core/match.c core/draft.c core/protocol.c core/card_rules.c core/card_text.c core/iduna.c core/http.c"
CLIENT_SRC="apps/client/main.c core/client.c core/policy.c core/protocol.c core/card_rules.c core/iduna.c core/http.c"
gcc $CFLAGS_BASE -O2 -pthread $SERVER_SRC -o build/dw_server
gcc $CFLAGS_BASE -O2 $CLIENT_SRC -o build/dw_client
BRAIN_SRC="core/brain.c core/bot_brain.c core/brain_default.c core/runtime/parena_runtime.c"   # bot_brain.c is PARENA-generated: needs card_rules.h forced in
BOT_SRC="apps/bot/main.c core/client.c core/policy.c core/protocol.c core/card_rules.c core/iduna.c core/http.c $BRAIN_SRC"
gcc $CFLAGS_BASE -include card_rules.h -O2 $BOT_SRC -lm -o build/dw_bot
./build/dw_bot --version
./build/dw_server --version
./build/dw_client --version

echo "== C: replay dump tool (WOTAN S547 -- real match-log replay, no server/bot binaries touched) =="
gcc $CFLAGS_BASE -O2 tools/replay_dump.c core/match.c core/card_rules.c -o build/dw_replay_dump
./build/dw_replay_dump < /dev/null >/dev/null 2>&1 || true   # smoke-check it runs (exits 2 on empty input, not a crash)

ARGS=" $* "
echo "== e2e: sanitized server + 3-bot pool + clients =="
tests/test_server_e2e.sh

if [[ "$ARGS" == *" --windows "* || "$ARGS" == *" --all "* ]]; then
  echo "== Windows cross-build (mingw) =="
  x86_64-w64-mingw32-gcc $CFLAGS_BASE -O2 $CLIENT_SRC -o build/dw_client.exe -lws2_32
  x86_64-w64-mingw32-gcc $CFLAGS_BASE -O2 -pthread $SERVER_SRC -o build/dw_server.exe -lws2_32
  x86_64-w64-mingw32-gcc $CFLAGS_BASE -include card_rules.h -O2 $BOT_SRC -o build/dw_bot.exe -lws2_32 -lm
  file build/dw_client.exe | grep -q PE32 
fi
if [[ "$ARGS" == *" --android "* || "$ARGS" == *" --all "* ]]; then
  echo "== Android: core JVM tests + APK =="
  "$BAZEL" run //android:core_test -- "$PWD/tests/parity_vectors.txt"
  "$BAZEL" build //android:deadweight
fi
if [[ "$ARGS" == *" --gui "* ]]; then
  echo "== GUI: dw_gui (SDL2) + headless selftest =="
  command -v pkg-config >/dev/null && pkg-config --exists sdl2 || { echo "--gui requires libsdl2-dev (pkg-config sdl2)"; exit 1; }
  # core/fx_rules.c is PARENA-generated (fx_rules.prn) and calls card_kind/kind_beats/card_power/
  # card_cost/card_keyword without its own prototypes -- same real reason core/bot_brain.c already
  # needs `-include card_rules.h` forced in (see BRAIN_SRC above), not a new pattern.
  GUI_SRC="apps/gui/main.c apps/gui/fx.c apps/gui/sfx.c core/client.c core/policy.c core/protocol.c core/card_rules.c core/fx_rules.c core/card_text.c core/iduna.c core/http.c"
  # S508e -- real TLS via PARENA's net/tls.prn (mbedTLS FFI, never hand-rolled crypto). Auto-
  # detected, not hard-required: without it, dw_gui still builds and runs, but any https:// IDUNA
  # URL (the real shipped default) fails clean at connect time instead of silently downgrading to
  # plaintext -- same "fail clean, never fall back to plaintext" contract core/http.c's own
  # dw_http() doc comment states. PARENA_RUNTIME_DIR/MBEDTLS_CFLAGS/MBEDTLS_LDFLAGS follow this
  # script's own SDL2_MINGW-style env-var-override convention.
  PARENA_RUNTIME_DIR="${PARENA_RUNTIME_DIR:-../PARENA/runtime}"
  MBEDTLS_CFLAGS="${MBEDTLS_CFLAGS:-}"
  MBEDTLS_LDFLAGS="${MBEDTLS_LDFLAGS:--lmbedtls -lmbedx509 -lmbedcrypto}"
  TLS_FLAGS=""
  TLS_SRC=""
  TLS_LIBS=""
  if [ -f "$PARENA_RUNTIME_DIR/parena_runtime.c" ] && echo '#include <mbedtls/ssl.h>' | gcc $MBEDTLS_CFLAGS -E - >/dev/null 2>&1; then
    echo "  (mbedTLS + PARENA runtime found -- building dw_gui with real TLS support)"
    TLS_FLAGS="-DPARENA_WITH_TLS -I$PARENA_RUNTIME_DIR $MBEDTLS_CFLAGS"
    TLS_SRC="$PARENA_RUNTIME_DIR/parena_runtime.c"
    TLS_LIBS="$MBEDTLS_LDFLAGS"
  else
    echo "  (skipping TLS: no mbedTLS/PARENA runtime found -- dw_gui will build but https:// IDUNA URLs will fail clean at connect time, never fall back to plaintext. Run sudo-queue/89-install-libmbedtls-dev.sh and ensure PARENA is checked out as a sibling repo to enable it.)"
  fi
  gcc $CFLAGS_BASE -include card_rules.h -O2 $(pkg-config --cflags sdl2) $TLS_FLAGS $GUI_SRC $TLS_SRC $(pkg-config --libs sdl2) -lm $TLS_LIBS -o build/dw_gui
  ./build/dw_gui --version
  tests/test_gui_selftest.sh
  if [[ "$ARGS" == *" --windows "* ]]; then
    SDLW="${SDL2_MINGW:-./sdl2_mingw}"
    if [ -d "$SDLW/include" ]; then
      echo "== GUI: Windows cross-build =="
      # S508f -- real TLS for the Windows build too, same real mbedTLS-via-PARENA-FFI as the
      # Linux build above, cross-compiled for mingw (mbedTLS's own plain Makefile supports
      # CC/AR overrides, no CMake needed -- see MBEDTLS_MINGW_DIR's own doc). Windows has no
      # single-file system CA bundle path the way Linux does, so the trust store is embedded
      # directly into the .exe instead (core/runtime/ca_bundle_data.h, generated by
      # scripts/gen_ca_bundle.sh, checked in -- see PARENA_TLS_CA_BUNDLE_EMBEDDED's own doc
      # comment in PARENA's runtime header). Auto-detected the same way the Linux TLS build
      # above is: builds without it if not found, https:// just fails clean at connect time.
      MBEDTLS_MINGW_DIR="${MBEDTLS_MINGW_DIR:-./mbedtls-mingw}"
      WIN_TLS_FLAGS=""
      WIN_TLS_SRC=""
      WIN_TLS_LIBS=""
      if [ -f "$MBEDTLS_MINGW_DIR/library/libmbedtls.a" ] && [ -f "$PARENA_RUNTIME_DIR/parena_runtime.c" ]; then
        echo "  (mbedTLS mingw build + PARENA runtime + embedded CA bundle found -- building dw_gui.exe with real TLS support)"
        WIN_TLS_FLAGS="-DPARENA_WITH_TLS -DPARENA_TLS_CA_BUNDLE_EMBEDDED -include core/runtime/ca_bundle_data.h -I$PARENA_RUNTIME_DIR -I$MBEDTLS_MINGW_DIR/include"
        WIN_TLS_SRC="$PARENA_RUNTIME_DIR/parena_runtime.c"
        WIN_TLS_LIBS="-L$MBEDTLS_MINGW_DIR/library -lmbedtls -lmbedx509 -lmbedcrypto"
      else
        echo "  (skipping Windows TLS: no mbedTLS mingw build at $MBEDTLS_MINGW_DIR -- dw_gui.exe will build but https:// IDUNA URLs will fail clean at connect time. See docs/WINDOWS_TLS_BUILD.md for how to produce that mingw build.)"
      fi
      x86_64-w64-mingw32-gcc $CFLAGS_BASE -include card_rules.h -O2 -I"$SDLW/include" -I"$SDLW/include/SDL2" $WIN_TLS_FLAGS $GUI_SRC $WIN_TLS_SRC -o build/dw_gui.exe \
        -L"$SDLW/lib" -lmingw32 -lSDL2 $WIN_TLS_LIBS -lws2_32 -lbcrypt -mwindows
      file build/dw_gui.exe | grep -q PE32
    else
      echo "(skipping Windows GUI: no SDL2 mingw tree at $SDLW; set SDL2_MINGW)"
    fi
  fi
fi
echo "BUILD CLEAN"
