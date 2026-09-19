#!/usr/bin/env bash
# Regenerate the checked-in C and Java rules from the canonical PARENA source, then regenerate parity vectors.
# The .prn lives in the PARENA repo (stdlib/deadweight/card_rules.prn); generated copies are checked in here
# (KARAMBIT/SPIDERBEETLE precedent: no live cross-repo build dependency).
set -euo pipefail
cd "$(dirname "$0")/.."
PARENA_BIN="${PARENA_BIN:-/home/fatbaby/PARENA/parena}"
PRN="${PARENA_PRN:-/home/fatbaby/PARENA/stdlib/deadweight/card_rules.prn}"
JAVA_OUT=android/src/main/java/industrial/einhorn/deadweight/generated/CardRules.java
"$PARENA_BIN" build "$PRN" -o core/card_rules.c
"$PARENA_BIN" build "$PRN" -o "$JAVA_OUT"
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
echo "regenerated core/card_rules.c, $JAVA_OUT, tests/parity_vectors.txt"
