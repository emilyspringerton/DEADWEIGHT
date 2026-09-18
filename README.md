# DEADWEIGHT — Dark Sector: Hold Battles

1v1 spatial-knapsack PvP. **VS0 = card mode**, Android-first, multiplayer + bots from day one.
Start at `docs/VS0_SCOPING.md`; rules in `docs/CARD_MODE_RULES.md`; wire format in `docs/WIRE_PROTOCOL.md`;
the long-horizon game analysis is `NORTHSTAR.md` (backpack-battler mode = VS1).

## Build (clean-build bar: this must be green before anything else merges)

```bash
scripts/build.sh              # C: ASan+UBSan rules tests, dw_server, dw_client
scripts/build.sh --windows    # + mingw cross-build of dw_client.exe / dw_server.exe
scripts/build.sh --android    # + plain-JVM parity test + APK (Bazel + rules_android)
scripts/build.sh --all
scripts/gen_rules.sh          # regenerate checked-in C/Java rules + parity vectors from PARENA's card_rules.prn
```

Android prerequisites: `ANDROID_HOME` with `platforms;android-34` + `build-tools;35.0.0` (see KARAMBIT's README for
no-sudo SDK install), and `BAZEL=/path/to/bazelisk` if `bazel` isn't on PATH. **Sandbox-only gotcha** (same as
KARAMBIT): this repo sits under `/home/fatbaby`, whose `go.work` leaks into rules_android's Go tool bootstrap —
run Bazel with `HOME=/tmp/dw-home` (`mkdir -p` it first). Not needed on CI or any machine without a parent `go.work`.

## One rules source, two targets, zero FFI

`PARENA/stdlib/deadweight/card_rules.prn` (scalar-only) compiles to `core/card_rules.c` (server/Windows/bots/
training) and `android/.../generated/CardRules.java` (Android). `tests/parity_vectors.txt` (1610 vectors, generated
from the C build) is verified by both `tests/test_rules.c` and `android/.../ParityTest.java`; `test_rules.c` also
carries a hand-typed oracle from the rules doc so the generated code can't grade itself.

## CI / releases

`.github/workflows/ci.yml`: every push builds/tests everything; every green push to `main` auto-bumps the minor
version and publishes a GitHub Release (server, Windows client, APK). Unlicense.
