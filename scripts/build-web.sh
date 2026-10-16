#!/usr/bin/env bash
# build-web.sh OUT_DIR -- build the DEADWEIGHT browser client into a deployable static tree:
#   OUT_DIR/index.html, OUT_DIR/dist/*.js (tsc), OUT_DIR/dist/generated/*.wasm + cards.json,
#   OUT_DIR/src/generated/cards.json
# Used by CI (.github/workflows/deploy-web.yml here, and WOTAN's deploy.yml) so the site served at
# wotan.okemily.com/DEADWEIGHT/ is always built from DEADWEIGHT main, never a hand-synced copy.
# Needs node/npm, and clang + wasm-ld (apt: clang lld) for the two native wasm32 modules.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: build-web.sh OUT_DIR}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"

(cd web && npm ci --no-audit --no-fund && npx tsc -p tsconfig.json)
bash scripts/build_wasm_native.sh
bash scripts/build_wasm_fx.sh
mkdir -p web/dist/generated
cp web/src/generated/cards.json web/dist/generated/cards.json

rm -rf "$OUT/dist" "$OUT/src"
mkdir -p "$OUT/src/generated"
cp web/index.html "$OUT/index.html"
cp -r web/dist "$OUT/dist"
cp web/src/generated/cards.json "$OUT/src/generated/cards.json"
for f in dist/main.js dist/generated/dw_protocol.wasm dist/generated/dw_fx.wasm; do
  [ -s "$OUT/$f" ] || { echo "build-web.sh: missing $f" >&2; exit 1; }
done
echo "build-web.sh: $OUT ready"
