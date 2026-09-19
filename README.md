# DEADWEIGHT — Dark Sector: Hold Battles

1v1 spatial-knapsack PvP. **VS0 = card mode**, Android-first, multiplayer + bots from day one. Card mode now ships a **73-card catalog** (the 9 triangle cards + 64 "Underwriters' Guild" cards with armor, credits, statuses, hand locks and control effects) over wire protocol **v2** — see `docs/CARD_MODE_RULES.md` for the full catalog. Deliberately **not** balanced by tooling: bad cards, trap cards, generally-strong staples and expensive control bombs are all on purpose. There are two queues: **random** (each player shuffles the whole catalog) and **draft** (each player first drafts a 16-pick, 23-card deck; after a game choose the same deck or redraft, bots keep a winning deck and redraft after a loss; every drafted deck is logged to `decks.ndjson`). Draft is implemented across the server, bots, Windows GUI and Android client and verified by unit + end-to-end tests (see `docs/CARD_MODE_RULES.md`); it is **not deployed to the live server yet**, the Android draft screens have only been type-checked against the SDK (never run on a device), and constructed (bring your own deck) is not built.
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
training) and `android/.../generated/CardRules.java` (Android). The card catalog and every card effect are *data* in that
file (packed effect words + one `fx-amount` function); `core/match.c` only sequences them. `tests/parity_vectors.txt`
(~14.7k vectors, generated from the C build, including a seeded sample of every card's effect amounts) is verified by
`tests/test_rules.c`, `android/.../ParityTest.java` and the Python port in `training/dw_rules.py`; `test_rules.c` also
carries a hand-typed oracle from the rules doc so the generated code can't grade itself. Retuning or adding a card =
edit the table + `scripts/gen_rules.sh`. (Regenerating needs a PARENA built after 2026-09-19: its Java emitter used to
truncate expressions longer than 511 characters.)

## CI / releases

`.github/workflows/ci.yml`: every push builds/tests everything; every green push to `main` auto-bumps the minor
version and publishes a GitHub Release (server, Windows client, APK). Unlicense.
