# DEADWEIGHT — Brand & Style Guide (v1, S513)

**Status: this is the first written version of this document.** There was no prior lore bible or
style guide to "reverse-engineer" — what existed before this pass was code (a real GUI color
palette, a real card-art generation pipeline) and scattered UI copy, not a canon document. This
guide makes that canon explicit for the first time, grounded in what's actually shipped wherever
possible, and flags plainly where the founder's stated direction (brutalist terminal) diverges
from what's currently rendered (the card art). See "The Real Starting Point" below before reading
further — it matters for how to use this doc.

---

## The Real Starting Point (read this first)

The founder's brief said "reverse-engineer your previous art generation scripts." One correction,
found while writing this: **there are no Python art scripts in this repo.** The real, live art
pipeline is `art/build_art.sh` (Bash) calling IDUNA's `cmd/nock` CLI, backed by ImageMagick 6 —
generates the 9 card faces, the card back, and 5 icons (offense/operations/defense/hull/energy),
shipped into `android/src/main/res/drawable-nodpi`. Not a huge distinction, but the hex codes
below are pulled from the *real* source (`art/recipes/lib.sh`), not invented, so the toolchain
matters.

There are actually **two live visual languages in this codebase right now**, and they don't
match each other:

1. **The GUI chrome** (`apps/gui/main.c`) — menu, HUD, Draft Hub, match screens. This one really
   is brutalist: flat rectangles, hard 2px frames, a hand-rolled 5x7 bitmap font, a dark
   navy/graphite palette, zero gradients, zero soft edges. **This is the part of DEADWEIGHT that
   already lives the "brutalist corporate terminal" pitch, today, shipped.**
2. **The card face art** (`art/recipes/lib.sh`, NOCK/ImageMagick) — gradient fills, tier rims in
   bronze/silver/gold, soft glyph shapes. This is closer to a polished sci-fi trading-card look
   than a brutalist terminal one. It is not wrong or bad — it's just a different, more illustrated
   register than the chrome around it.

This guide treats **the GUI chrome as the real, current brand voice** (Section 2A below) and
documents the card-art palette as-is (Section 2B) without pretending they're unified. If "fully
embracing brutalist" means flattening the card art to match the chrome — killing the gradients,
tier rims, and soft glyphs in favor of flat two-tone shapes and hard edges — that's a real,
scoped follow-up (`art/recipes/lib.sh` rewrite), not something this document does silently. Naming
it here so it's a decision, not a drift.

---

## 1. Core Aesthetic & Lore Pillar

**The vibe:** Brutalist. Unpolished on purpose. Nothing here is trying to look expensive — no
chrome bevels, no particle-effect flourishes, no "welcome, hero" softness. A terminal, not a
storefront. Every screen looks like something a systems administrator would actually build:
function first, legibility first, zero ornament. The bitmap font and flat 2px frames already in
`apps/gui/main.c` are not a placeholder aesthetic to be replaced later — they're the target.

**The lore:** You are not a hero. You are not named, not voiced, not given a backstory cutscene.
You are an anonymous intruder — a **Runner** — who has broken into **IDUNA**, the corporate
identity/defense mainframe running this ecosystem, to extract proprietary R&D and escalate your
own access before the system notices and kills the connection. Every match is a live intrusion
into hostile, automated territory. Every session is borrowed time.

This is not adopted retroactively — it's a close, honest fit for what's already real in this
codebase: IDUNA genuinely is the platform's real, central identity/auth/economy authority (see
`IDUNA/docs/NORTHSTAR.md`, "IDUNA is not a product, it is the backbone"), and DEADWEIGHT genuinely
does route every player action through it. The fiction isn't decorating a generic card game; it's
describing, almost literally, what the client is actually doing on the wire.

---

## 2A. Visual Language — GUI Chrome (real, shipped, the brand's true voice)

Pulled verbatim from `apps/gui/main.c`'s own `Col` constants (0–255 RGB, converted to hex below).

| Name | Hex | RGB | Used for |
|---|---|---|---|
| **Terminal Black** | `#12141C` | 18, 20, 28 | Screen background |
| **Corporate Grey** | `#202432` | 32, 36, 50 | Panel/box fill |
| **Signal White** | `#EBEBF0` | 235, 235, 240 | Primary text |
| **Dim Grey** | `#82879B` | 130, 135, 150 | Secondary/dim text, labels |
| **Clearance Green** | `#50C878` | 80, 200, 120 | Success, "PLAY AGAIN," good outcomes |
| **Error Red** | `#E15046` | 225, 80, 70 | Failure, Burned Proxies, defeat |
| **Cursor White** | `#FFFFFF` | 255, 255, 255 | Active focus / selection frame |
| **Locked Grey** | `#5A5A69` | 90, 90, 105 | Inactive/locked frame |
| **Access Gold** | `#FFC83C` | 255, 200, 60 | FOUNDER tag, premium/paid accents |

Kind colors (the offense/operations/defense triangle, same palette used on both the HUD and the
card art below — this is the one place the two visual languages already agree):

| Kind | Hex | RGB |
|---|---|---|
| Offense (red) | `#D7463C` | 215, 70, 60 |
| Operations (yellow) | `#E1A028` | 225, 160, 40 |
| Defense (blue) | `#468CE6` | 70, 140, 230 |

**Typography:** Monospace only, everywhere. The shipped renderer uses a hand-rolled 5x7 bitmap
font (`apps/gui/main.c`'s `FONT` table) — no anti-aliasing, no kerning finesse, deliberately
retro-terminal. Any future real typeface choice should preserve that: a true monospace (IBM Plex
Mono, JetBrains Mono, or a genuine pixel font), never a humanist sans, never anything with warm
rounded terminals. All-caps for UI labels is already the convention (`"TICKETS: %d/%d"`,
`"DRAFT HUB"`, `"ABORT & EXTRACT"`) — keep it; lowercase reads as a friendlier register than this
game should ever use.

**Motifs:**
- **Hard rectangles, 2px frames.** No rounded corners anywhere in the shipped chrome.
- **Flat fills, zero gradients** in the UI layer (gradients are card-art-only, see 2B).
- **Binary state indicators**, not decorative ones — the Draft Hub's 3 "Burned Proxy" boxes are
  the model: a filled/red square or an empty/panel square, nothing in between, no icon needed.
- **No iconography where text will do.** The bitmap font already reads as terminal-native; a
  hand-drawn icon set would fight it, not complement it.
- Not currently in the codebase but consistent with the pillar and worth adopting deliberately if
  budget allows: **scanlines / CRT flicker** as a subtle full-screen post-process, **dashed
  borders** for "unconfirmed/pending" states (as distinct from the solid frames used for
  confirmed/locked state), and **raw ASCII/box-drawing characters** (`┌─┐│└─┘`) for panel borders
  instead of solid-fill rectangles, if the bitmap font ever needs a companion box-drawing set.

---

## 2B. Visual Language — Card Face Art (real, shipped, different register)

For completeness and honesty, not because it fits the brutalist pillar. From
`art/recipes/lib.sh`:

| Name | Hex | Role |
|---|---|---|
| Offense gradient top | `#E86A5F` | Kind-colored card face gradient, light end |
| Offense gradient bottom | `#6E1512` | Kind-colored card face gradient, dark end |
| Operations gradient top | `#F2C14E` | " |
| Operations gradient bottom | `#7A5A00` | " |
| Defense gradient top | `#6A9BF5` | " |
| Defense gradient bottom | `#182F6B` | " |
| Bronze tier rim | `#CD7F32` | Common-tier card border |
| Silver tier rim | `#C8CDD3` | Uncommon-tier card border |
| Gold tier rim | `#FFD24A` | Rare-tier card border |
| Card back gradient (top) | `#2B2F3A` | Card back fill |
| Card back gradient (bottom) | `#0E1016` | " |
| Emblem stroke (outer) | `#8892A6` | Card back ring detail |
| Emblem stroke (inner) | `#FFD24A` | Card back ring detail |

Motifs: gradient fills top-to-bottom per kind, a tier-colored rim, and three glyph shapes per
kind drawn as filled polygons (16-point star = Offense, hexagon-with-inner-ring = Operations,
hex-shield-with-cross = Defense) — see `art/recipes/lib.sh`'s `glyph()` function for the exact
polygon math if regenerating.

---

## 3. Tone of Voice (Copywriting Guide)

**How DEADWEIGHT speaks:** Clinical. Merciless. Systemic. It never addresses the player as "you,
hero" — it logs them as a process. No exclamation points celebrating a win; a win is just a
correct read that the system will now try to correct for. No apology for a loss; the system does
not apologize, it terminates the connection. Copy reads like log output or a terminal prompt, not
a mascot talking. Compare: **not** "Nice job! You beat the AI!" — **instead** `"INTRUSION
SUCCESSFUL. HULL BREACH CONFIRMED."` or nothing at all (silence and a hard cut is often more
correct than a line of dialogue).

**Canon glossary** (founder-specified; adopt going forward, migrate existing copy toward it over
time rather than in one disruptive pass):

| In-universe term | Mechanical meaning | Current shipped copy (as of this doc) |
|---|---|---|
| **Network Breach** | A match | `"MATCH"` / `"ROUND %d/%d"` — not yet renamed |
| **Proxy** | A life in a Draft Run (max 3) | Already exact: `"BURNED PROXIES"`, Draft Hub UI |
| **Abort & Extract** | Voluntarily ending an active run early | Already exact: literal button label, `draft-run/abort` |
| **Corporate Defense System / Hunter-Killer** | A matchmaking bot opponent | Currently just `"(BOT)"` tag next to the opponent name — not yet renamed |
| **Root Access Override** | The $15 Premium claim code | Already exact: S510's own pricing copy |
| **Runner** | The player | Already exact: S512's auto-generated guest names (`Runner-A7B2`) |

Two of six are already exact matches with zero changes needed — the naming direction was already
converging on this before it was written down. The two gaps (`"MATCH"` → Network Breach, `"(BOT)"`
→ Corporate Defense System/Hunter-Killer) are real, scoped, low-risk copy-only follow-ups, not
done in this pass.

---

## 4. The "Do Not Do" List

- **No fantasy elements.** No swords-and-sorcery card names, no "mana," no dragons. Every card
  name, every stat, every mechanic reads like it belongs to a real intrusion toolkit (malware,
  exploits, hardware) — see the existing 105-card catalog for the bar already set.
- **No shiny mobile-game buttons.** No gradients, no drop shadows, no bevels, no "juicy" bounce
  animations on the UI chrome. A button is a flat rectangle with a 2px frame and text in it.
- **No friendly tutorial pop-ups.** No mascot explaining the rules with a speech bubble. If
  something needs explaining, it's terse, monospace, and reads like a man page — not a wizard.
- **No cute subtitles.** Nothing like "League of Mercenaries," "Card Clash Arena," or any
  fantasy/sports-league framing. The real subtitle already in `README.md` — "Dark Sector: Hold
  Battles" — and any future tagline should sound like a redacted internal project name, not a
  marketing line.
- **No exclamation points in system copy.** Ever. The system does not get excited.
- **No player avatars, portraits, or character customization.** The Runner is deliberately
  faceless — that's the point, not a missing feature.
- **No soft/rounded UI geometry.** Rounded corners, soft shadows, pill-shaped buttons — all off
  brand. Hard rectangles only.

---

## 5. Future Art Direction — Cutscenes / Title Screen (explicitly not current scope)

Per the founder's own framing: this is a "when we get more revenue" idea, not a commitment or a
build item. Captured here so the idea has a real home instead of living only in chat.

Two reference images were shared as mood/inspiration, not as templates to reproduce — noted here
in terms of the *qualities* worth borrowing, not as source material to copy:

- A retro-futuristic vessel-interior scene: exposed piping, analog gauges, warm practical lighting
  against a cold blue exterior view (aurora/ice through a porthole) — the "lived-in machine, not a
  sterile sci-fi set" quality is the useful takeaway for any future IDUNA/mainframe environment
  art, not the specific scene or characters.
- An anime-styled cockpit scene: cel-shaded linework, a muted cyan/green starfield against warm
  cockpit red/amber instrument light, CRT-adjacent scanline texture. The **linework-over-flat-color
  cel-shading technique** and the **cool-exterior/warm-interior light contrast** are both real,
  reusable direction for a title screen or cutscene register — a harder, more graphic-novel style
  than the card art's soft gradients, and notably *closer to the brutalist pillar* than either of
  today's two in-repo visual languages.

**A real, new lore idea worth recording, not yet canon:** the founder's own suggestion — a
spacesuit-clad, blue-eyed pilot figure as the visual embodiment of **EmilyOS, "the Ledger," the
custodian of reality** in this universe's lore. If DEADWEIGHT ever gets a title screen or
cutscene budget, this is a real, strong hook: the Ledger as a watching, recording presence behind
the intrusion the player is running — the system's own conscience/witness, rendered once, in a
higher-fidelity register than anything else in the game, specifically *because* she's the one
thing in this universe that isn't brutalist-terminal-flat. Contrast, not consistency, is the
point of her appearing this way. Not scoped, not built, no code or asset work follows from this
section — recorded as a real creative decision point for whenever the budget conversation is
real.

---

*This document supersedes no prior style guide (none existed). Update it in place as the brand
solidifies — don't fork a v2 unless the pillar itself changes.*
