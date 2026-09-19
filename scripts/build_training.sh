#!/usr/bin/env bash
# Lean build for training machines (Colab): just build/dw_server, no tests, no sanitizers, no Android/Bazel.
# The full clean-build bar lives in scripts/build.sh; this is only what the league trainer needs.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
gcc -std=c99 -O2 -Wall -Icore -Icore/runtime -DPARENA_NO_GRAPHICS -DDW_VERSION=\"training\" -pthread \
    apps/server/main.c core/match.c core/protocol.c core/card_rules.c core/iduna.c core/http.c -o build/dw_server
./build/dw_server --version
