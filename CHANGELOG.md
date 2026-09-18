# CHANGELOG

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
