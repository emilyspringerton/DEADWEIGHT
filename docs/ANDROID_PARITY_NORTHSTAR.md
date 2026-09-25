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
or (c) something else entirely. Flagged in `EMILY/BACKLOG.md`; not built until scoped.

## Current real state (checked directly)

- **Desktop client** (`apps/gui/main.c`): hand-rolled SDL2 immediate-mode brutalist renderer —
  flat rectangles, 2px frames, a 5x7 bitmap font, cards drawn as vector shapes/text at render
  time (`card_box()`), not image assets. ~a few thousand lines of C, single-file.
- **Android client** (`android/src/main/java/...`): ~2100 lines of Java. Real: wire protocol
  codec (`core/Protocol.java`), session/socket transport, draft model, and PARENA-generated
  scalar decision logic (`generated/CardRules.java`, from `PARENA/stdlib/deadweight/
  card_rules.prn`). **Not real yet: no rendering/UI layer at all** — no Activity, no View, no
  Canvas/OpenGL drawing code exists in this repo today. "Bring to parity" is not a reskin of an
  existing screen; it is building the Android UI layer from zero.
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

## Real, phased plan

1. **Rendering port** — a Java Canvas-based renderer replicating the desktop's flat-rect/2px-
   frame/bitmap-font style: port the palette constants and glyph table from `apps/gui/main.c`
   into a shared resource (constants class + bitmap font asset), build the actual Android
   Activity/View/Canvas draw loop (does not exist yet).
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
