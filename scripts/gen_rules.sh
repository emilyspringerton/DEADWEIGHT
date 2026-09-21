#!/usr/bin/env bash
# Regenerate the checked-in C and Java rules from the canonical PARENA source, then regenerate parity vectors.
# The .prn lives in the PARENA repo (stdlib/deadweight/card_rules.prn); generated copies are checked in here
# (KARAMBIT/SPIDERBEETLE precedent: no live cross-repo build dependency).
set -euo pipefail
cd "$(dirname "$0")/.."
PARENA_BIN="${PARENA_BIN:-/home/fatbaby/PARENA/parena}"
PRN="${PARENA_PRN:-/home/fatbaby/PARENA/stdlib/deadweight/card_rules.prn}"
FX_PRN="${PARENA_FX_PRN:-/home/fatbaby/PARENA/stdlib/deadweight/fx_rules.prn}"
JAVA_OUT=android/src/main/java/industrial/einhorn/deadweight/generated/CardRules.java
TS_OUT=web/src/generated/CardRules.ts
FX_TS_OUT=web/src/generated/FxRules.ts
"$PARENA_BIN" build "$PRN" -o core/card_rules.c
"$PARENA_BIN" build "$PRN" -o "$JAVA_OUT"
# Browser client (web/): same one-source-two-targets discipline as the Java build above, using
# PARENA's real TypeScript emitter (src/emit_ts.c). Checked in, not built at web-client build time
# -- same KARAMBIT/SPIDERBEETLE convention the Java output above already follows.
mkdir -p "$(dirname "$TS_OUT")"
"$PARENA_BIN" build "$PRN" -o "$TS_OUT"
# Round-resolution animation/audio DECISION layer (Windows is canonical, browser copies it exactly
# -- founder real-time 2026-09-21: "use parena to unify the windows and TS versions... dogfood it,
# eat more of the app"). Compiled SEPARATELY from card_rules.prn (not combined into one file) so
# core/fx_rules.c doesn't re-emit card_kind/kind_beats/etc. and collide at link time with
# core/card_rules.c's own copies -- it calls them via core/fx_rules.h's own extern declarations
# instead (same -include card_rules.h convention core/bot_brain.c already established, see
# scripts/build.sh). core/fx_rules.h itself is hand-written (curated subset of the generated
# functions apps/gui/fx.c actually calls), same convention as core/card_rules.h.
"$PARENA_BIN" build "$FX_PRN" -o core/fx_rules.c
# TS has no C-style link-time duplicate-symbol error (each file is its own module scope), so the
# browser build compiles card_rules.prn + fx_rules.prn TOGETHER into one self-contained file --
# simpler than post-processing an ES import statement onto fx_rules.prn's own calls into
# card-kind/kind-beats/card-power/card-cost/card-keyword. main.ts still imports card-level lookups
# from CardRules.ts (the one canonical import site for those); FxRules.ts's own duplicated copies
# of them are only ever called from inside FxRules.ts itself.
"$PARENA_BIN" build "$PRN" "$FX_PRN" -o "$FX_TS_OUT"
# Java emitter derives the package from the output path; verify rather than assume.
grep -q '^package industrial.einhorn.deadweight.generated;' "$JAVA_OUT" || {
  sed -i '1a\\npackage industrial.einhorn.deadweight.generated;' "$JAVA_OUT"; }
gcc -std=c99 -Wall -Wextra -Icore -Icore/runtime -DPARENA_NO_GRAPHICS tests/gen_vectors.c core/card_rules.c -o /tmp/dw_gen_vectors
/tmp/dw_gen_vectors > tests/parity_vectors.txt
# Java client card text: generated from the same C table (names, rules text, kind/keyword names).
gcc -std=c99 -Wall -Wextra -Werror -Icore -Icore/runtime -DPARENA_NO_GRAPHICS apps/tools/cards_java.c core/card_rules.c core/card_text.c -o /tmp/dw_cards_java
/tmp/dw_cards_java > android/src/main/java/industrial/einhorn/deadweight/CardText.java
# Bot brain (S503-14b): PARENA decision math -> C. Compiled with -include card_rules.h (the emitter references
# card_kind/card_power/kind_beats without prototypes). Skipped quietly if the .prn isn't present.
BRAIN_PRN="${PARENA_BRAIN_PRN:-/home/fatbaby/PARENA/stdlib/deadweight/bot_brain.prn}"
if [ -f "$BRAIN_PRN" ]; then "$PARENA_BIN" build "$BRAIN_PRN" -o core/bot_brain.c; fi
echo "regenerated core/card_rules.c, core/fx_rules.c, $JAVA_OUT, $TS_OUT, $FX_TS_OUT, tests/parity_vectors.txt"
