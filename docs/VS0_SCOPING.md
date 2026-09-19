# DEADWEIGHT VS0 — scoping pass (2026-09-18, S503)

Founder brief (real-time, `emily observe` Apple #20124): ship VS0 as an **Android app**, built the Emily
Way, PARENA-first, multiplayer + bots from day one, CI/CD releases from the first commit, clean builds
first. This doc supersedes `NORTHSTAR.md`'s D1–D6 *ordering* for VS0 only (its game analysis and V0 item
cut stand). Contracts: `CARD_MODE_RULES.md`, `WIRE_PROTOCOL.md`.

## 1. What VS0 is (and is not)

**VS0 = card mode, end to end, over the real network, against real bots, with real CI releases.**

| In VS0 | Deferred (named, not dropped) |
|---|---|
| Card mode: 1v1, simultaneous-lock rounds, 9 cards, hidden hands | Backpack battler (6x6 grid): **VS1**, per `PHASE_D1_CORE_LOOP.md`; wire protocol reserves `mode=1` now |
| Server-authoritative TCP server, multi-match, `--fast-forward` | Ultimates/Flow, derivatives market, tournaments, 2v2 (`NORTHSTAR.md` cuts stand) |
| Bot pool of 3 standing heuristic bots on the same matchmaker humans use | Learned bots in the live pool (league trains them; heuristic bots ship first) |
| Training league scaffold: packet-level env, 3-role PFSP + Elo, registry push | Actual long training runs (Colab, follow-up) |
| Android APK (hand-written Java shell + PARENA-generated `CardRules.java`) | Play Store signing/track |
| Windows: cross-compiled server + headless client in CI | SDL2 Windows card UI (VS0.5) |
| IDUNA: `game='deadweight'`, bot/RL agents, guest accounts, game-scoped checkpoint registry | Guest→email upgrade path |

Why card mode first: no inventory/placement UI to build on a phone, discrete per-round actions (small
action space → trivial to mask for RL, trivial to fast-forward, trivial for a heuristic bot), and it shares
the item triangle (Offense / Operations / Defense) with the backpack mode so the cards become the backpack items'
stat block later instead of a throwaway.

## 2. Architecture

```
 Android (Java shell)          Windows (C headless→SDL2)        dw_bot x3 (C, pool)     rl_env_packet.py
   │ TCP frames                   │ TCP frames                     │ TCP frames            │ TCP frames
   └──────────────┬───────────────┴────────────────┬──────────────┴───────────────────────┘
                  ▼                                 ▼
            dw_server (C, poll(), N matches/process) ──POST results / verify──► IDUNA :8080
              └─ links card_rules.c  (PARENA → C)                                 game='deadweight'
   Android links CardRules.java (PARENA → Java, same .prn)                        DEADWEIGHT-BOTS / -RL agents
                                                                                   checkpoint registry (game-scoped)
```

Key calls (each is a decision, not a question — veto any of them):

1. **One rules source, two emit targets, zero FFI.** `PARENA/stdlib/deadweight/card_rules.prn` is written
   in the scalar subset both emitters already support (I32/Bool params, single-expression `defn`,
   `if`/arithmetic/comparison, calls between defns). C build = authoritative server + Windows + bots;
   Java build = client-side legality/preview helpers. **Nothing is added to PARENA's Java emitter's FFI
   surface, and the Java side makes no syscalls** (firm constraint, honored). Cross-target correctness is
   proven, not assumed: a generated **parity vector file** (every input combination → expected output)
   is checked by both a C test and a plain-JVM Java test in CI.
2. **Stateful match logic lives in hand-written C**, calling the PARENA rules — the monorepo's universal
   precedent (REDGARDEN/ECOWAR: "PARENA is the decision, host does the work"). The Java emitter's real
   ceiling (scalar single-expression `defn`, `PARENA/src/emit_java.c`, 391 lines) is *not* worked around
   with FFI; if a later VS needs structs/loops in Java, that is emitter work in PARENA, scoped separately.
3. **TCP, not UDP.** Turn-based, ~20 tiny frames per match; reliability matters, twitch latency doesn't;
   Java `Socket` is plain Java. BRAWLPIT's *training pipeline* is what we copy 1-for-1 (packet-level env →
   real wire protocol, fast-forward server, 3-role PFSP league, Elo, registry), not its UDP transport.
   LZ4 convention: frames are <100 bytes, compression buys nothing — no deviation needed, nothing to compress.
4. **One `dw_server` process hosts many matches** (unlike arena_server's one-match-per-process): matches are
   a few hundred bytes of state, so a single `poll()` loop is simpler and lets the 3-bot pool + humans share
   one queue. `--fast-forward` disables round timers so training/bot-vs-bot runs at CPU speed; `--port` lets
   the training league run several isolated servers (separate league, same binary).
5. **Bot pool = 3 standing `dw_bot` processes** playing the archetype triangle (Ripper=Offense-heavy,
   Wall=Operations-first control (historical name), Mirror=counter). They queue into the same matchmaker as humans. Pairing rule
   (server): *never pair the last waiting bot with another bot*, so ≥1 bot is always available for a human
   (founder manual testing) while the other two fight and exercise the server 24/7. Live-pool bots later pull
   league checkpoints from the registry (BRAWLPIT `rl_bot_pool.py` precedent); VS0 ships heuristics honestly
   labeled as such.
6. **Training league = same server binary, separate ports/league dir**, `--fast-forward`. Roles: Main
   (PFSP), Main Exploiter, League Exploiter; one snapshot cycle registers all 3 (BRAWLPIT's
   `register_generation_snapshot`), Elo per checkpoint. `rl_league.py` is ported verbatim (pure Python, no
   coupling; 29 upstream tests come with it). Action masking (`MaskablePPO`) fits the 5-action space. Real
   training runs happen on Colab; VS0 proves the loop with a smoke test.
7. **IDUNA is the guide repo, multi-tenant by default** (founder: "the answer is probably almost always yes"):
   - checkpoint registry: **generalize** `brawlpit-checkpoints` to a `game`-scoped registry rather than
     copy it a third time (`shankpit-checkpoints` + `brawlpit-checkpoints` already exist — this is the
     "third copy" moment; DEADWEIGHT is the forcing function). Existing routes keep working.
   - players scoped by `game` (`202609050003_players_game_scope.sql`), agents `DEADWEIGHT-BOTS` (bot play /
     match results) and `DEADWEIGHT-RL` (checkpoint upload), `ECOWAR-BOTS` precedent.
   - guest accounts: `provider="guest"`, name-only, client-held secret (D2 design in
     `PHASE_D2_SERVER_AND_ACCOUNTS.md` stands: no token = no recovery, by design).
   - server↔IDUNA: server verifies the connecting player's token and posts results; mirror ECOWAR's
     `arena_server` precedent where it exists. `--no-auth` exists for dev/training only.
   - **Not** making the matchmaker/server itself a shared multi-game binary in VS0 (PARENGINE_NORTHSTAR
     already tracks that duplication problem); it takes `--game`/`mode` as data so the door stays open.
8. **Art via NOCK** (`IDUNA/internal/nock`, `cmd/nock`): card art = NOCK procgen/texture store pulled at
   build time or via HTTP. **No PARENA texture FFI** (firm constraint).
9. **Android build = Bazel + `rules_android`** (KARAMBIT precedent, proven with this box's SDK), not
   Gradle. Core Java (protocol codec, session state machine, generated `CardRules`) lives in a plain
   `java_library` testable on a bare JVM with zero Android SDK; the Activity/UI is a thin shell over it.

## 3. CI/CD (first-commit bar)

`.github/workflows/ci.yml`, same shape as KARAMBIT/BRAWLPIT: every push builds and tests; every green push to
`main` auto-bumps the **minor** version, tags, and cuts a GitHub Release (non-prerelease, so
`marcosnils/bin` works) with `dw_server` (Linux), `dw_client.exe` (Windows, mingw), `deadweight.apk`.
Jobs: **core** (gcc, `-Wall -Wextra -Werror`, ASan+UBSan test run), **windows** (mingw cross-build),
**android** (SDK + `bazel test //android:core_test` + `bazel build //android:deadweight`), **release**
(needs all three). "Clean builds first": the skeleton with all three green locally ships before any
feature lane merges; every lane keeps `bash scripts/build.sh` clean.
Version stamping: `versionCode = github.run_number`, `versionName = vX.Y.0` (KARAMBIT's sed approach).

## 4. Lanes (parallelizable once contracts are committed)

| Lane | Backlog | Owner deliverable | Touches |
|---|---|---|---|
| A | S503-01/02 | this doc, contracts, skeleton, CI, first release | DEADWEIGHT, EMILY |
| B | S503-03/04/05 | `card_rules.prn` (in PARENA), C core, `dw_server`, `dw_bot`, headless tests | PARENA, DEADWEIGHT |
| C | S503-06 | IDUNA game scope, agents, guest provider, generalized registry, Go tests | IDUNA |
| D | S503-07 | Android app + core JVM tests + APK | DEADWEIGHT/android |
| E | S503-08 | league env + PFSP port + smoke test | DEADWEIGHT/training |

Lane B/C/D/E acceptance shares one bar: builds clean from a fresh checkout, tests pass, no live/prod
service is restarted (throwaway instances only — `IDUNA_PRO`-isolation and `pkill`-exact-PID lessons apply).

## 5. Honest risks

- **Rules are a proposal.** `CARD_MODE_RULES.md` is *my* minimal VS0 design (the source transcript has no
  card mode — only a derivatives card deck we deferred). Numbers live in one `.prn` file; tune freely. The
  triangle is guess-heavy by nature — that's what the PFSP league is for, and the first real playtest
  answers whether it's fun before we build backpack mode on top.
- **Java emitter ceiling** means Android carries *helpers*, not the authoritative game. Acceptable: server
  is authoritative anyway.
- **CI Android job is the first place SDK/Bazel drift shows up**; KARAMBIT's own history (the `yes|sdkmanager`
  SIGPIPE bug) is pre-fixed in the workflow.
- **`parena` binary in CI.** Generated C/Java are checked in (KARAMBIT/SPIDERBEETLE precedent, no live
  cross-repo Bazel dep); a local script regenerates and CI fails if generated files drift from the `.prn`
  only when `parena` is available — VS0 keeps the `.prn` canonical in the PARENA repo, generated copies here.
- **Server↔IDUNA auth from C** is the least-precedented piece; if verification turns out heavier than a
  loopback HTTP call, VS0 falls back to name-only + result posting under the bot/agent identity and files the
  gap, rather than adding crypto to C in a hurry.
