#!/usr/bin/env bash
# Regenerate the card catalog JSON the WOTAN deck browser reads (tooltips). Re-run after any card change.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-../WOTAN/data/cards.json}"
mkdir -p "$(dirname "$OUT")"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
gcc -std=c99 -Wall -Wextra -Werror -Icore -Icore/runtime -DPARENA_NO_GRAPHICS apps/tools/cards_json.c core/card_rules.c core/card_text.c -o "$T/cards_json"
"$T/cards_json" > "$OUT"
[ "${2:-}" = "--md" ] && "$T/cards_json" --md
echo "wrote $OUT ($(grep -c '"id"' "$OUT") cards)"
