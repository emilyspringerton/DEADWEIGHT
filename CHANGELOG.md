# CHANGELOG

## 2026-09-19
- feat: scripts/export_cards.sh + apps/tools/cards_json.c -- card catalog JSON for WOTAN's deck browser (sess-20260918-1725-497f394f)
- feat(draft): draft mode -- separate queue, 16-pick 23-card deck, same-deck/redraft (bots keep winners), decks.ndjson log; server, bots, Windows GUI, Android; not deployed yet (sess-20260918-1725-497f394f)
- fix(training): Colab script/notebook clone the public repo over plain https -- no GitHub token prompt (sess-20260918-1725-497f394f)
- docs(training): shared registry live on okemily.com (IDUNA redeployed, DEADWEIGHT-RL provisioned); real push + resume verified (sess-20260918-1725-497f394f)
- feat(gui): card effect text always visible on every card (hand 2x2 grid, revealed cards too), 2x text with word wrap, falls back to 1x if it doesn't fit; effectless cards show no text; window 480x956; added & ' ; $ = glyphs; verified via headless selftest frames (sess-20260918-1725-497f394f)
- feat(gui): Windows client defaults to okemily.com:6980 (was 127.0.0.1:7700) (sess-20260918-1725-497f394f)
- feat(training): training/colab_train.py one-cell Colab bootstrap + dw_train --resume-from-registry; first real MaskablePPO smoke run + resume verified locally; live IDUNA registry route not deployed yet (sess-20260918-1725-497f394f)
- feat: round limit 8 -> 100, round timer 20s -> 40s; server play-log buffer sized for 100 rounds (was fixed 8) (sess-20260918-1725-497f394f)

- feat(card-mode): 64 new cards (73 total) -- armor, credits, statuses, hand locks, cancel/copy/swap control, Dark Pool; wire protocol v2; data-driven effect engine in PARENA; C==Java==Python parity over ~14.7k vectors; PARENA Java emitter truncation bug fixed (PARENA 8141f8f). Draft/deckbuilding is next (not in this ship) (sess-20260918-1725-497f394f)


## 2026-09-18
- docs(card_expansion): graded Haiku's card-expansion proposal + meta prompt, wrote proposal review, metaprompt engineering pass, and v2 meta prompt (not yet run against a model) (sess-20260918-1725-497f394f)
- fix(android): LOCK IN crashed the app -- Session sent frames on the UI thread (NetworkOnMainThreadException); all outbound writes now go through a single writer thread + regression test (sess-20260918-1725-497f394f)
- S503-14b: dw_bot hybrid brain -- hand-written MLP (PARENA bot_brain.prn -> core/bot_brain.c) blended with heuristic archetype prior; distilled default weights embedded; --brain/--weights/--wh/--wn/--sigma; tests + win-rate table (sess-20260918-1725-497f394f)
- S503-09: dw_gui SDL2 card client (Windows+Linux), build.sh --gui, headless selftest, CI gui job + release packaging (sess-20260918-1725-497f394f)
- S503-10: NOCK-built card art (art/build_art.sh) wired into CardView with shape fallback (sess-20260918-1725-497f394f)
- training: AUTH frame (0x06) in codec/env/fake server; integration + dry-run trainer verified against real dw_server; fixed ghost-queue pairing on opponent shutdown (sess-20260918-1725-497f394f)
- Android: AUTH frame for full IDUNA tokens, 1024 max frame, integration test vs real dw_server+dw_bot (S503-07) (sess-20260918-1725-497f394f)
- lane B: wire protocol codec + AUTH frame, deterministic match core, dw_server/dw_client/dw_bot, 3-bot pool, e2e under ASan/UBSan, IDUNA auth+result reporting, systemd unit files (S503-04/05) (sess-20260918-1725-497f394f)
- training/: PFSP 3-role league (ported), game-scoped registry client, wire-protocol env + fake server, dw_train dry-run, Colab stub (S503-08) (sess-20260918-1725-497f394f)
- Android VS0: plain-JVM core (codec/transport/session/model), programmatic card UI, IDUNA guest auth hook, 118-check CoreTest; APK builds clean (S503-07) (sess-20260918-1725-497f394f)
- VS0 scoping pass (`docs/VS0_SCOPING.md`, `CARD_MODE_RULES.md`, `WIRE_PROTOCOL.md`): card mode first, Android-first,
  bots + multiplayer from day one, backpack battler = VS1. (S503-01)
- Clean-build skeleton: PARENA-generated `card_rules` (C + Java from one `.prn`), 1610 cross-target parity vectors +
  hand-typed oracle test, `dw_server`/`dw_client` stubs, Windows mingw cross-build, Bazel APK, CI with auto minor
  releases. Local `scripts/build.sh --all` clean. (S503-02/03)
