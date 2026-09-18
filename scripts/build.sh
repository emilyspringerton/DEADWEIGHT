#!/usr/bin/env bash
# Clean build + test. Usage: scripts/build.sh [--windows] [--android] [--all]
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

echo "== C: server + client =="
gcc $CFLAGS_BASE -O2 apps/server/main.c core/card_rules.c -o build/dw_server
gcc $CFLAGS_BASE -O2 apps/client/main.c core/card_rules.c -o build/dw_client
./build/dw_server --version
./build/dw_client --version


ARGS=" $* "
if [[ "$ARGS" == *" --windows "* || "$ARGS" == *" --all "* ]]; then
  echo "== Windows cross-build (mingw) =="
  x86_64-w64-mingw32-gcc $CFLAGS_BASE -O2 apps/client/main.c core/card_rules.c -o build/dw_client.exe -lws2_32
  x86_64-w64-mingw32-gcc $CFLAGS_BASE -O2 apps/server/main.c core/card_rules.c -o build/dw_server.exe -lws2_32
  file build/dw_client.exe | grep -q PE32 
fi
if [[ "$ARGS" == *" --android "* || "$ARGS" == *" --all "* ]]; then
  echo "== Android: core JVM tests + APK =="
  "$BAZEL" run //android:core_test -- "$PWD/tests/parity_vectors.txt"
  "$BAZEL" build //android:deadweight
fi
echo "BUILD CLEAN"
