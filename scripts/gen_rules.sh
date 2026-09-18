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
echo "regenerated core/card_rules.c, $JAVA_OUT, tests/parity_vectors.txt"
