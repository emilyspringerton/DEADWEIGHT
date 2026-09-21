# DEADWEIGHT — Dark Sector: Hold Battles

A fast, mean, 1v1 sci-fi card battler. Two commanders, one Dark Sector holding, and a catalog of
105 cards you'll learn to distrust — because not everything that looks good *is* good. Built
Android-first, with a real matchmaking server behind it, so you can play a stranger online or
warm up against the bot pool. This is a small, sharp game, not a slot-machine live-service pile —
what's here works, and what doesn't exist yet is called out honestly below.

## The pitch

You and your opponent each command a ship with **20 hull, 2 energy, 3 credits, and no armor**.
Every round you both secretly lock in one move — play a card from your hand of 4, or pass to bank
energy — and then both moves resolve at once. No watching your opponent think. No stalling. Just
a clean, simultaneous read on what they're about to do, round after round, for up to 100 rounds.

Every card in the game is one of three kinds, and the kinds form a triangle:

- **Offense (Red)** — direct attacks, missiles, kinetic force.
- **Operations (Yellow)** — active tactical plays: hacking, jamming, maneuvering.
- **Defense (Blue)** — barriers, repair, armor, reserves.

**Offense beats Operations. Operations beats Defense. Defense beats Offense.** Reading your
opponent's kind, not just their card, is half the game.

Operations cards go further — every one of them carries a named keyword that tells you what kind
of trouble it is before you've even read the rules text:

| Keyword | What it means at the table |
|---|---|
| **Lock** | Precision targeting. Pierces armor, guarantees damage that can't be reflected. |
| **Sabotage** | Viruses and hijacks. Burns you over time, cancels your play, disables cards sitting in your hand. |
| **Flank** | Positional strikes. Bonus damage against Defense, or against an opponent who just passed. |
| **Scan** | Intel. Reveals your hand, your hull, your energy, your credits — then punishes what it finds. |
| **Siphon** | Resource theft. Drains your energy and credits straight into your opponent's pocket. |

The catalog is **deliberately not balanced by a program**. There are genuine traps, cards that
read as strong and aren't, quietly excellent staples nobody notices at first, and a handful of
expensive control bombs that can swing a whole match if you let them sit uncontested. Learning
which cards only *look* good is part of learning to play.

## Play your way in

Queue up random, or draft first:

- **Random queue** — every player shuffles the full 105-card catalog. Fast, chaotic, no prep.
- **Draft queue** — draft a 23-card deck from a run of picks before the match starts, then either
  keep that deck for your next game or redraft. Every drafted deck gets logged, so the meta is
  visible, not folklore.

## Real online multiplayer, from day one

DEADWEIGHT isn't bots-only with multiplayer bolted on later — it shipped with a real,
server-authoritative matchmaker from the start. A single dedicated server hosts many matches at
once over a lightweight TCP wire protocol, so queueing into a live opponent and queueing into a
bot feel the same from the client's side. No opponent online, or just want to warm up? A standing
pool of heuristic bots sits on the same matchmaker humans use — no separate "practice mode," it's
the same queue. Every match, human or bot, is tracked the same way: results, ratings, and drafted
decks all land in the same place, so the game keeps a real record of how you're playing, not just
whether you won the last round.

## Where to start reading

- `docs/VS0_SCOPING.md` — what card mode is, architecture, what's in vs. deferred
- `docs/CARD_MODE_RULES.md` — the full rules and 105-card catalog
- `docs/WIRE_PROTOCOL.md` — the network protocol clients speak to the server
- `NORTHSTAR.md` — the long-horizon game design analysis

## Honest status

Card mode (random queue, live server, bots, Android client) is real and playable today. A few
things are further along than others, and it's worth being precise about which:

- **Rounds resolve with full animation and synthesized audio** in the Windows client — clash
  scenes per triangle outcome and keyword, criticals, resource/status effects, the works (see
  `docs/ANIMATION_AND_AUDIO.md`). The Android client doesn't have this yet, and the audio has been
  verified numerically, not by ear.
- **Draft mode** is implemented end-to-end — server, bots, Windows GUI, Android client — and
  covered by unit and end-to-end tests. It is **not deployed to the live server yet**, and the
  Android draft screens have only been type-checked against the SDK, never run on a real device.
- **Constructed decks (bring your own deck)** — not built.
- **Accounts (IDUNA guest auth, tiered Draft tickets, redeem codes, Uncapped Draft Runs)** are real
  and live against production. The GUI client speaks real TLS (mbedTLS, bound via PARENA —
  `stdlib/net/tls.prn`, never hand-rolled crypto) and defaults to the real production IDUNA URL.
  The **Linux build is live-verified end to end** with the actual shipped binary (real handshake,
  real cert verification, real account created against `https://okemily.com`). The **Windows
  cross-build (`dw_gui.exe`) compiles and links clean** with a real mbedTLS mingw build and a real
  embedded CA bundle (see `docs/WINDOWS_TLS_BUILD.md`), but has **not been runtime-verified** — no
  Wine/Windows environment exists to actually execute the `.exe` and confirm a live handshake, so
  don't treat it as proven the way the Linux build is until someone runs it for real.
- **Draft Runs actually spend and pay out tickets now (S510)** — readying the Itch.io launch plan
  (free-to-play with a 25-ticket/day guest cap, a $15 "Root Access Override" Premium code as the
  soft upsell) found this was never true: the DRAFT button's `TICKETS: N` gate was purely
  cosmetic, and the "Uncapped Draft Run" auto-end never actually granted any tickets back. Both
  are now real, atomic IDUNA endpoints (`/draft-run/start`, `/deck`, `/abort`, plain `GET`, all
  player-token-direct, same trust level as `/redeem`) with a real cash-out table (0 tickets under
  6 wins, up to +18 at 25+, `draftRunReward` in `IDUNA/internal/http/handlers/game_online.go`).
  The Windows/Linux GUI has a real **Draft Hub** screen (win count, 3 "Burned Proxy" loss
  indicators, deck list, Resume Uplink, Abort & Extract) reachable both mid-run and on a cold
  boot with an active run — reconnecting into a persisted deck goes through a new wire message
  (`DW_C_DRAFT_RESUME`) that re-validates the deck's draft-legality server-side
  (`dw_draft_deck_valid`) rather than trusting whatever the client sends. **Not yet built: the
  Android client's own Draft Hub** (`DraftModel.java` still predates this work) — this pass only
  touched the C GUI and the server/IDUNA sides.

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

## One rules source, three targets, zero FFI

`PARENA/stdlib/deadweight/card_rules.prn` (scalar-only) compiles to `core/card_rules.c` (server/Windows/bots/
training), `android/.../generated/CardRules.java` (Android), and `web/src/generated/CardRules.ts` (browser). The card
catalog and every card effect are *data* in that file (packed effect words + one `fx-amount` function); `core/match.c`
only sequences them. `tests/parity_vectors.txt` (~14.7k vectors, generated from the C build, including a seeded sample
of every card's effect amounts) is verified by `tests/test_rules.c`, `android/.../ParityTest.java`, the Python port in
`training/dw_rules.py`, and (manually, via `web/bridge/e2e_test.mjs`) the TypeScript build; `test_rules.c` also carries
a hand-typed oracle from the rules doc so the generated code can't grade itself. Retuning or adding a card = edit the
table + `scripts/gen_rules.sh`.

## Browser client (dev, VS0.5-web)

`web/` is a real, working browser client, dogfooding PARENA's TypeScript emitter against this repo's own
`card_rules.prn` — connects over a WebSocket↔TCP bridge to the real `dw_server`, plays the real wire protocol, and
calls the PARENA-compiled `isLegalPlay`/`cardKind`/`cardKeyword` directly rather than re-implementing rules in hand-
written JS. Round-resolution animation and audio are unified too: `PARENA/stdlib/deadweight/fx_rules.prn` compiles to
both `core/fx_rules.c` (Windows, canonical) and `web/src/generated/FxRules.ts` (browser), so the SAME scenario/winner/
critical/timeline decisions drive both clients — the browser then renders them with its own Canvas2D/Web Audio, the
same "PARENA decides, host renders" split every DEADWEIGHT client already follows. Verified end-to-end: a real match
played start to finish against a live bot through the compiled client (`web/bridge/e2e_test.mjs`), including real fx
timelines computed against real match data. Random queue only, no auth, not deployed anywhere — see `web/README.md`
for the full honest status (including what the fx animation/audio port does and doesn't cover yet) and how to run it
locally. Several real, load-bearing bugs in PARENA's TypeScript emitter were found and fixed getting this far (I32
division truncation, a 511-character buffer overflow, a missing `(not x)`, unrecognized `true`/`false` literals) —
see `PARENA/STDLIB.md`'s own TypeScript emitter section.

## CI / releases

`.github/workflows/ci.yml`: every push builds/tests everything; every green push to `main` auto-bumps the minor
version and publishes a GitHub Release (server, Windows client, APK). Unlicense.

---

Long-term, DEADWEIGHT isn't just the card battler — it's the front end of a bigger idea, a
spatial-knapsack "backpack battler" where you pack physical cargo into a grid and that same
layout becomes your ship's combat loadout. **That mode does not exist yet** — it's a design-only,
long-horizon plan (`NORTHSTAR.md` calls it VS1), with no code written toward it. Everything
above this line is the real game, playable today.
