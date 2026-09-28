# Android Parity — NORTHSTAR (S513 reversal, 2026-09-25)

## What changed

`docs/BRAND_STYLE_GUIDE.md` (S513, same day) recorded the Android client as shelved: "we are
abandoning the android aesthetic totally... going all in on the desktop clients." Founder
real-time direction received today reverses that: bring the Android client to full feature and
visual parity with the desktop (Windows/Linux SDL2) client, sharing the same art and rendering
approach, plus copy/paste support and a "key unlock" feature set. `docs/BRAND_STYLE_GUIDE.md`
Section 2A/2B language has been updated to note the reversal; this doc is the real scoping pass
for the actual parity work, per this repo's own "spec before implementation" convention.

**Open question, not guessed at:** no "key unlock" feature exists anywhere in this repo today —
not in `NORTHSTAR.md`, `docs/PHASE_D5_D6_UI_AND_LAUNCH.md`, the wire protocol, or either client.
Before implementing it, this needs a real answer from the founder: does "key unlock" mean (a) an
Ultimates/cosmetic unlock progression system, (b) an IDUNA account/license-key entitlement gate,
or (c) something else entirely. Flagged in `EMILY/BACKLOG.md` SECTION 555 (logged 2026-09-27 — the
first version of this doc claimed it was flagged, but no entry had actually landed); not built
until scoped.

## Current real state (checked directly)

- **Desktop client** (`apps/gui/main.c`): hand-rolled SDL2 immediate-mode brutalist renderer —
  flat rectangles, 2px frames, a 5x7 bitmap font, cards drawn as vector shapes/text at render
  time (`card_box()`), not image assets. ~a few thousand lines of C, single-file.
- **Android client** (`android/src/main/java/...`): ~2100 lines of Java. Real: wire protocol
  codec (`core/Protocol.java`), session/socket transport, draft model, PARENA-generated scalar
  decision logic (`generated/CardRules.java`, from `PARENA/stdlib/deadweight/card_rules.prn`),
  and — **correction, found on closer read; the first pass of this doc missed the top-level
  package and wrongly said no UI existed** — a real, working `MainActivity`/`CardView`/`BarView`
  UI: standard Android widgets (Button/TextView/LinearLayout) for menu/queue/draft/match screens,
  `CardView` drawing the old NOCK `card_<id>.png` art (falling back to a plain rounded rect),
  `BarView` drawing hull/energy meters. So "bring to parity" is a **re-render** of an already
  functional screen flow, not a from-zero build — the state machine, screens, and interactions
  are done; only the visual language (Canvas draw calls in `CardView`/`BarView`, plus
  `MainActivity`'s stock widget styling) needs to become brutalist to match the desktop client.
- **Art**: `art/build_art.sh` + `art/recipes/lib.sh` (NOCK/ImageMagick gradient card art) is the
  old, now-superseded Android-only pipeline. Per the founder's own new direction, the target is
  no longer that pipeline — it's the desktop client's vector/bitmap-font rendering *approach*,
  ported to Android's Canvas API (flat rects, 2px frames, same hex palette, same bitmap font
  glyph data), not a new art-asset pipeline.
- **PARENA abstraction**: `card_rules.prn` (scalar rules) and `fx_rules.prn` (round-resolution
  animation/audio decisions, shared today between Windows and the browser client per the root
  CLAUDE.md's D2/DEADWEIGHT stack section) are the existing real precedent for "use PARENA to
  abstract stuff" — the natural extension is compiling `fx_rules.prn` to Java as well
  (`scripts/gen_rules.sh` already emits `android/.../generated/CardRules.java`; add an
  `FxRules.java` target the same way) so animation/timeline decisions are identical across
  Windows, browser, and Android, not reimplemented a third time by hand.

## Status (2026-09-27)

- **Phase 1 — done** (DEADWEIGHT f0ec008). Compile-checked 2026-09-27 against real `android.*` API
  stubs (Maven Central `com.google.android:android:4.1.1.4`, `javac` over every `src/main` file):
  clean. Still never run on a device/emulator.
- **Phase 2 — done** (2026-09-27). `generated/FxRules.java` (third target of `fx_rules.prn`, combined
  with `card_rules.prn` exactly like the TS build) + `core/FxTimeline.java` (port of
  `web/src/fx.ts`'s `computeTimeline`) + `FxView.java` (brutalist reveal: flip-in, winner frame,
  impact flash, crit shake, fx.c's own labels/colours, HULL/ARMOR/ECON/STATUS stage strip). The
  final round's reveal finishes before the result screen. Verified by `//android:fx_parity_test`
  (9,941 C->Java vectors + 27 hand-computed FxTimeline scenario checks, in CI), a stale-vector
  guard in `scripts/build.sh`, and `IntegrationTest` computing a timeline for every real
  ROUND_RESULT against a live `dw_server` + `dw_bot`. **Not done**: audio, particles/ships; energy-
  delta and new-status stages (same wire-protocol gap as the browser, see `web/src/fx.ts`); any
  on-device run.
- **Phase 1B — menu/title screen parity + zero-friction auth — done** (2026-09-27, founder
  real-time: "port more of the deadweight graphics into parena we need full java parity including
  the launcher title screen it should look just like our windows app"). Phase 1 re-themed
  `CardView`/`BarView` (in-match UI) but left `MainActivity`'s login/menu screen on raw
  `android.widget.EditText`/`Button`/`TextView` requiring manual name/host/port/IDUNA-URL entry —
  a genuinely different app from the Windows client's own zero-friction `draw_boot()`/
  `draw_menu()`, not just a color mismatch. Real, found-live gap fixed along the way: `Config.
  DEFAULT_IDUNA_URL` was `""` (Android had no IDUNA account, no tickets, ever, by default) while
  the desktop client's own S508d fix pointed it at the real, live `https://okemily.com` months
  ago — Android's default now matches.
  - New `PixelLabel.java` (one line of `PixelFont` text, self-sizing) and `BrutButton.java` (flat
    filled rect + centered `PixelFont` label, dimmed when disabled) — the menu-screen equivalents
    of `text()`/`text_c()`/`button()`. `Theme.java` gained `BLUE`/`FOUNDER_GOLD` (the two menu
    colors that weren't already `KIND_COLOR` reuse — DRAFT's orange genuinely IS
    `KIND_COLOR[1]`/Operations in the C source, not a coincidence).
  - `MainActivity.bootstrapAuth()` ports `iduna_bootstrap()`'s real shape, once at launch: try a
    saved guest login, fall through to registering a brand-new lore-named guest on ANY failure
    (network or rejected) — the one genuinely surprising behavior kept intentionally, since
    that's the real desktop client's own accepted risk, not a bug to fix while porting. `GuestAuth`
    gained `tickets`/`isGuest` on `Result`, plus `redeem()`/`upgrade()`/`emailLogin()` (mirroring
    `dwi_redeem`/`dwi_guest_upgrade`/`dwi_email_login`), including the 409-is-not-a-failure
    login-fallback `do_link_email` establishes.
  - Menu now shows: title, name + ticket count (or the real reason there's no account, not a
    generic "offline"), a session-local FOUNDER tag after a founder-granting redeem (matches
    `A.is_founder` exactly — not persisted/re-fetched on a later boot, on either client), DRAFT
    (disabled at 0 tickets)/PRACTICE brutalist buttons, REDEEM CODE, and CLAIM ACCOUNT (guest) /
    CONNECTION SECURED (claimed). No manual name/host/port/IDUNA-URL fields anywhere.
  - Verified: `javac --release 8` against the real `com.google.android:android:4.1.1.4` stub jar
    (Phase 1's own method) — clean, zero warnings, every `src/main` file. `CoreTest`'s real
    `guestAuth()` case (a live `com.sun.net.httpserver.HttpServer` fake IDUNA, plain JVM, no
    Android runtime needed) — 228 checks, 0 failures, confirming `GuestAuth`'s rewritten
    constructor/response-parsing didn't regress register/login/`AuthRejected`. Bazel wasn't
    available in this sandbox to re-run `//android:core_test`/`//android:fx_parity_test`, but
    neither test imports anything this pass touched (only `generated.CardRules`/`FxRules`) --
    confirmed by reading their imports, not assumed.
  - **Honest, named simplifications, not hidden**: Claim Account is a native `AlertDialog` with
    two theme-colored `EditText`s, not a fully custom-rendered modal like `draw_claim()` — real
    touch-keyboard capture into a hand-drawn field is a materially bigger lift than two lines of
    text entry justify. Never run on a device/emulator (same limitation Phase 1/2 already have).
    FRIENDS & DUELS (`S_SOCIAL`) has no Android equivalent at all yet — not a redraw, a whole new
    feature (friend lists, requests, duel challenges) with no client-side code to re-skin; left
    out of the menu entirely rather than shipped as a dead button, named as real future work below
    rather than silently dropped.
- **Phase 1C — queue/lobby/draft/match-end screens — done** (2026-09-28, part of the founder's
  broader "get DEADWEIGHT at parity with windows for the android and wasm client... all the ui all
  the affordances" ask). `renderQueue`/`renderLobby`/`renderDraft`/`renderEnd` in `MainActivity`
  now build `PixelLabel`/`BrutButton` (same primitives Phase 1B's menu pass already established)
  instead of `TextView`/`Button`/`ScrollView`+`TextView` — matching `apps/gui/main.c`'s
  `draw_queue()`/`draw_draft()`/`draw_end()` content and layout exactly (queue now shows the real
  waiting count from `S_QUEUED`'s own payload, previously received but never displayed; draft's
  1x/2x/3x pick buttons and LEAVE are brutalist; match-end shows the same big VICTORY/DEFEAT/DRAW
  plus hull comparison and reason). `renderMatch`'s PASS/LOCK IN buttons were left as-is (out of
  this phase's scope — Phase 1 already brutalist-rendered the rest of that screen).
  - **Two real, named gaps found and left un-built, not silently reskinned as if complete**:
    (1) Android has no Draft Hub screen at all (`apps/gui/main.c`'s `S_DRAFT_HUB` — win-streak
    "burned proxies" tracker, RESUME UPLINK/ABORT & EXTRACT) and no client-side win/loss-streak
    state to back one; a draft-mode match end goes straight to SAME DECK/REDRAFT, same as before
    this pass. Building the Hub is new stateful feature work, not a UI reskin, and doing it under
    this phase's "reskin existing screens" banner would have meant either skipping real state
    tracking or over-scoping a supposedly-mechanical pass — named here as real, deferred work
    instead. (2) Social (Friends & Duels, `S_SOCIAL`) still has no Android screen at all — already
    named as "a whole new feature, not a redraw" in Phase 1B above; unchanged this pass.
  - Verified: `javac --release 8` against the real `com.google.android:android:4.1.1.4` Maven
    Central stub jar — clean, every `src/main` file (same method Phase 1/1B used; the jar itself
    had to be re-fetched this session, wasn't cached). Bazel was checked again and is still not
    available in this sandbox (no `bazel`/`bazelisk` binary anywhere on `PATH` or common install
    locations) — same limitation Phase 1B hit. Found a working full JDK in this sandbox that Phase
    1B didn't use (`EINHORN_SURVIVAL/jdk25`, real `javac`+`java` 25.0.4 — the previously-tried
    `.local/opt/jdk-21` install has a broken `libjli.so`), which made it possible to compile and
    **actually run** all four plain-JVM test mains directly, not just `CoreTest`: `CoreTest` (228
    checks), `ParityTest` against the real, current `tests/parity_vectors.txt` (24,097 vectors, 0
    failures — CardRules.java's C parity), `core.FxParityTest` against `tests/fx_parity_vectors.txt`
    (9,941 vectors / 9,968 checks, 0 failures — FxRules.java's C parity), and `IntegrationTest`
    against the real, already-built `build/dw_server`/`build/dw_bot` binaries (a full real match,
    6 rounds, plus a real draft-to-deck-to-requeue flow, 0 failures) — a strictly higher
    verification bar than any prior phase reached in this sandbox, though still never run on an
    actual Android device/emulator (none exists here, same limitation every phase has named).
- **Phase 3 / 4 — not started** (phase 4 still blocked on the open question above). Tracked in
  `EMILY/BACKLOG.md` SECTION 555.

## Real, phased plan

1. **Rendering port** — port the desktop's `Col` palette and 5x7 bitmap-`FONT` table
   (`apps/gui/main.c`) into `Theme.java`/`PixelFont.java`, and re-implement `CardView.onDraw`/
   `BarView.onDraw` to draw the brutalist flat-rect/2px-frame/bitmap-font style (matching
   `card_box()`) instead of the NOCK PNG art + system font. Retire the NOCK art bitmaps from the
   live render path (kept in `res/drawable-nodpi` as a historical asset per
   `BRAND_STYLE_GUIDE.md`'s own precedent, not deleted outright, but no longer drawn). Re-theme
   `MainActivity`'s stock widget backgrounds/text colors to the same palette — the screen flow
   and state machine are already real and don't change.
2. **PARENA-driven fx parity** — extend `scripts/gen_rules.sh` to also emit `FxRules.java` from
   `fx_rules.prn`, wire it into the new Android renderer's round-resolution timeline, matching
   `apps/gui/fx.c` and `web/src/fx.ts`'s existing decision calls.
3. **Copy/paste** — scope which surfaces need it (deck codes / draft loadout export-import is the
   only plausible candidate found in this repo; there's no other text-entry surface). Uses
   Android's standard `ClipboardManager`; no PARENA involvement needed, this is host-platform
   glue.
4. **Key unlock** — blocked on the open question above. Not started.

## Explicitly deferred / not this pass

Full parity (all four phases) is not landing in one commit — matches this repo's own "no
half-finished implementations" and "spec before implementation" conventions. Phase 1 is the
critical path everything else depends on (there is no Android screen to unlock or paste into
yet). See `EMILY/BACKLOG.md` for the tracked, sequenced items.
