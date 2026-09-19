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
  GUI_SRC="apps/gui/main.c core/client.c core/policy.c core/protocol.c core/card_rules.c core/card_text.c core/iduna.c core/http.c"
  gcc $CFLAGS_BASE -O2 $(pkg-config --cflags sdl2) $GUI_SRC $(pkg-config --libs sdl2) -o build/dw_gui
  ./build/dw_gui --version
  tests/test_gui_selftest.sh
  if [[ "$ARGS" == *" --windows "* ]]; then
    SDLW="${SDL2_MINGW:-./sdl2_mingw}"
    if [ -d "$SDLW/include" ]; then
      echo "== GUI: Windows cross-build =="
      x86_64-w64-mingw32-gcc $CFLAGS_BASE -O2 -I"$SDLW/include" -I"$SDLW/include/SDL2" $GUI_SRC -o build/dw_gui.exe \
        -L"$SDLW/lib" -lmingw32 -lSDL2 -lws2_32 -mwindows
      file build/dw_gui.exe | grep -q PE32
    else
      echo "(skipping Windows GUI: no SDL2 mingw tree at $SDLW; set SDL2_MINGW)"
    fi
  fi
fi
echo "BUILD CLEAN"
