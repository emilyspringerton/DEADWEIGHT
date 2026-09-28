# DEADWEIGHT native WASM client — NORTHSTAR (2026-09-28)

## Source

Founder real-time (routed via `emily observe` first, per the standing protocol):

1. "we need to get DEADWEIGHT at parity with windows for the android and wasm (new) client eat
   the codebase as much as you can with PARENA share assets it should look the same in all the
   platforms all the ui all the affordances WASM version needs to work with IDUNA sso seamlessly
   wotan.okemily.com/DEADWEIGHT for the client" (Apple #21166).
2. "spin up an agent to start figuring out auto deploy..." — a separate, parallel workstream,
   see `docs/WASM_DEPLOY_NORTHSTAR.md` (Apple #21168).
3. Correction, same session: **"do not use emscripten it doesnt work it needs to be native wasm
   like mixforge"** (Apple #21169). This doc covers the client itself, post-correction.

## What "native wasm like mixforge" actually means, checked directly

`MIXFORGE/web/room.wasm` and `web/dsp.wasm` (real, live, already shipped) are built by
`scripts/build_room_wasm.sh`/`build_dsp_wasm.sh`:

```
parena build stdlib/mixforge/room.prn -o room.ll     # PARENA's own LLVM emitter (src/emit_llvm.c)
llc -mtriple=wasm32-unknown-unknown -filetype=obj room.ll -o room.o
wasm-ld --no-entry --export-all --allow-undefined -o room.wasm room.o
```

Loaded in the browser with zero glue: `await WebAssembly.instantiate(bytes, {})`, then call
`instance.exports.next_seat(...)` directly. No Emscripten SDK, no generated JS runtime, no libc
emulation, no virtual filesystem — the exact opposite of what an Emscripten build produces (which
is *also* real wasm, but wrapped in ~200KB of generated JS providing a simulated POSIX
environment). The founder tried the Emscripten path (this repo's own first attempt at this ask,
below) and rejected it outright.

**The one real difference from MIXFORGE's own pipeline**: MIXFORGE's wasm modules compile from
PARENA source (`.prn` → PARENA's LLVM emitter → `.ll`). DEADWEIGHT's client logic — the wire
protocol, draft/match/policy logic, the whole `apps/gui/main.c` state machine — is hand-written C,
not PARENA source (only `card_rules.prn`/`fx_rules.prn`/`account_rules.prn` are PARENA today).
The honest equivalent one step earlier in the same toolchain: skip `parena build`, go straight
from hand-written C to LLVM IR via `clang`'s own frontend, then the identical `llc`/`wasm-ld`
backend MIXFORGE already uses. Verified this session that this actually works: the same no-sudo
LLVM-18 toolchain MIXFORGE's build scripts already use (`$HOME/.local/opt/llvm-toolchain`) can
compile ordinary freestanding C straight to a working `wasm32-unknown-unknown` module —

```
clang -target wasm32-unknown-unknown -nostdlib -O2 -c file.c -o file.o
wasm-ld --no-entry --export-all --allow-undefined -o out.wasm file.o
```

— zero Emscripten anywhere in the chain. This is the real pipeline `scripts/build_wasm_native.sh`
(new, this session) uses.

## The abandoned first attempt (Emscripten) — kept here honestly, not deleted

Before the correction landed, this session got `apps/gui/main.c` + `core/*.c` (the *entire* real
desktop brutalist SDL2 client, unmodified game logic) compiling and running under Emscripten
(`emcc -sUSE_SDL=2`, using the SDL2 Emscripten port) — a genuine, verified, pixel-parity-by-
construction result (774KB `.wasm` + 193KB generated glue `.js`), since the brutalist renderer
draws only flat rects + a bitmap font with zero external image/font assets, and one real,
load-bearing portability bug was found and fixed along the way: `core/runtime/parena_runtime.h`'s
PTY host-glue section (`forkpty`, used by `stdlib/pty.prn`'s real host binding) had only a
`#ifndef _WIN32` / Windows-ConPTY-stub split, predating any non-POSIX-non-Windows target existing
in this monorepo — Emscripten's libc has no `forkpty` at all (no process model in a wasm sandbox,
not a "not implemented yet" gap). Fixed with a third branch (`#if !defined(_WIN32) &&
!defined(__EMSCRIPTEN__)` / honest `-1`-returning stub, matching the file's own existing Windows-
stub convention) — **this fix is real, harmless, and kept** regardless of the Emscripten-vs-native
decision, since it's a correct, narrowly-scoped portability fix to a shared vendored header, not
Emscripten-specific plumbing.

The founder rejected the Emscripten *approach* itself (the SDK, the generated JS runtime, the
~200KB glue overhead, the SDL2-emulation-over-a-simulated-environment shape of it) — not this one
bug fix. Networking/IDUNA-auth were never wired for the Emscripten attempt before the correction
arrived, so no further Emscripten-specific work exists to unwind.

## Real, verified progress this session (native wasm32, no Emscripten)

**`apps/wasm/`** — the wasm export boundary around DEADWEIGHT's actual C, not a reimplementation:

- `libc_shim.c` — the *only* three libc functions anything here calls (`memcpy`/`memset`/
  `strlen`), hand-written in a few lines each. `wasm32-unknown-unknown` ships no libc at all (not
  a smaller one — none), so freestanding code either avoids libc entirely or provides it itself;
  DEADWEIGHT's protocol/rules code already only touches these three.
- `string.h` — a local stub declaring exactly those three functions, since there's no real
  `<string.h>` to include on this target and `core/protocol.c` does `#include <string.h>`.
- `protocol_wasm.c` — real, named setter/getter exports for **every message type** in
  `docs/WIRE_PROTOCOL.md` (client→server: HELLO, QUEUE, PLAY, LEAVE, PING, AUTH, DRAFT_PICK,
  DRAFT_RESUME; server→client: WELCOME, QUEUED, MATCH_FOUND, ROUND_START, PLAY_ACK, PLAY_REJECT,
  ROUND_RESULT, MATCH_END, PONG, DRAFT_OFFER, DRAFT_DONE, ERROR) wrapping `dw_encode`/`dw_decode`
  from `core/protocol.c` — **compiled into this module completely unmodified**. This is the real
  "eat the codebase" deliverable: the browser gets DEADWEIGHT's actual wire codec, not a third
  hand-port (the existing `web/src/proto.ts` is a manual TypeScript port of
  `docs/WIRE_PROTOCOL.md` — a second, independent implementation of the same logic, with its own
  independent bug surface; this wasm module makes that duplication avoidable going forward).

**Named setters/getters, never raw memory-offset poking from JS** — this is a real, found bug
class, not a stylistic choice: the first version of this proof hand-computed `DwMsg` union field
offsets from JS assuming no padding, and silently wrote into the wrong field once `DwMsg`'s union
(4-byte-aligned, because other members contain `uint32_t` fields) padded 3 bytes after the leading
`uint8_t type` — every write landed 3 bytes into what was actually the *next* field over. Named
setters/getters that call real C field accesses (`g_msg.u.hello.proto`, etc.) make this whole bug
class structurally impossible from the JS side.

**`scripts/build_wasm_native.sh`** — mirrors `MIXFORGE/scripts/build_dsp_wasm.sh`'s own shape
(finds `clang`/`wasm-ld` under `LLVM_TOOLCHAIN_ROOT` or PATH, builds, runs a real test, fails
loudly if either binary or the build itself is missing). Output: `web/dist/generated/dw_protocol.wasm`
(**15.9KB** — compare to the abandoned Emscripten attempt's 774KB `.wasm` + 193KB `.js`; this is
what "native" actually buys, not just an ideological preference).

**`tests/test_wasm_protocol.mjs`** — real, live verification, not just "it compiles": 9 checks,
all passing —
- Round-trips HELLO/QUEUE/PLAY/PING/DRAFT_PICK through the module's own real `dw_encode`/
  `dw_decode` (encode with the real setters, decode the module's own output, check the real
  getters match).
- Independently decodes **hand-crafted wire bytes** (computed directly from
  `docs/WIRE_PROTOCOL.md`'s byte layout, not from this module's own encoder) for WELCOME,
  MATCH_FOUND, MATCH_END, and ERROR — proving the getters read real, independently-authored wire
  bytes correctly, not just bytes this same code already wrote.
- Does **not** re-prove `core/protocol.c`'s own wire-format correctness end to end —
  `tests/test_protocol.c` (the real C oracle, exhaustive fuzz coverage, run via `scripts/build.sh`)
  already does that, unmodified and untouched by this work. This test's actual job is proving the
  wasm *export boundary* doesn't corrupt anything crossing into/out of linear memory — the exact
  bug class the first attempt hit.

Run: `scripts/build_wasm_native.sh` (needs `node` on PATH and the LLVM-18 toolchain — see script
header for exact requirements).

## Update (2026-09-28, same session): rendering + networking now wired in and live-verified

The "extend the existing renderer" plan below was carried out, not just proposed: `web/src/
client.ts` now imports `web/src/wasmProto.ts` (a real, drop-in, wasm-backed replacement for
`proto.ts` — identical exported functions/types, so this was a two-line import swap plus one
`await initWasmProto()` call in `main.ts`'s `start()`) instead of the old hand-written TS codec.
`proto.ts` itself is kept as-is, now serving only as the independent oracle
`web/bridge/test_wasm_proto_parity.mjs` checks the wasm codec's output against (14 checks, byte-
and object-identical for every case checked). The existing real end-to-end harness
(`web/bridge/e2e_test.mjs`, already proven against the old codec on 2026-09-21) was re-run against
the new one, unmodified except for the wasm-fetch/WebSocket environment shims a Node harness
needs: a complete, real match against a live `dw_bot` over a live `dw_server` and the real WS↔TCP
bridge, 7 rounds played, 7 fx timelines computed, 0 failures — using the exact compiled
`dist/client.js`/`dist/wasmProto.js` a real browser would load. See `web/README.md`'s own
"Verified (2026-09-28)" section for the full account. Rendering and networking (items 1-2 below)
are therefore done.

## Update (2026-09-28, same session, continued): IDUNA SSO wired; hosting infra started

Item 3 (IDUNA SSO) is now wired: `web/src/sso.ts` (new) + `account.ts`'s new `loginWithSso` reuse
WOTAN/friends.html's already-live pattern (`iam.okemily.com` redirect → fragment token →
`POST /api/v1/games/deadweight/sso-exchange`). Verified against the real local IDUNA instance
(a deliberately bad token returned a real `401`, matching a raw `curl` to the same endpoint) —
not click-through-tested in a real browser (no headless Chrome in this sandbox). See
`web/README.md`'s "Verified (2026-09-28, continued)" section for the full account.

Item 4 (hosting) is now fully code-complete; only a human/sudo step remains. Built:
`DEADWEIGHT/ops/systemd/dw-ws-bridge.service` (loopback-only `ws-tcp-bridge.js` against the live
`dw_server` on `:6980`, live-verified by hand — started it, sent a raw WS frame, got a real
relayed byte response back), `WOTAN/ops/nginx-wotan.conf`'s new `/DEADWEIGHT/ws` location
(terminates `wss://`, proxies to the loopback bridge), and `WOTAN/DEADWEIGHT/` (the real built
client bundle — `index.html` + `dist/` + the compiled `.wasm` + `src/generated/cards.json` — a
plain copy of this repo's own `web/`, matching WOTAN's own no-build-step convention). Live-verified
before committing: served the copied tree with a plain HTTP server, confirmed every asset path the
page references resolves, then fetched the `.wasm` over that real HTTP connection and instantiated
it (98 exports). All 7 WOTAN pages now link to it ("Play" in the shared nav).

**Update: `~/wotan-deploy.sh` run for real** — `/var/www/wotan` is writable by this user directly
(no sudo needed for the rsync itself), so the actual deploy happened this session, not just the
commit. Live-verified against the real domain: `curl -s -o /dev/null -w '%{http_code}'
https://wotan.okemily.com/DEADWEIGHT/` → `200`, same for `dist/main.js` and
`dist/generated/dw_protocol.wasm`. **The static client is genuinely live at
`wotan.okemily.com/DEADWEIGHT` right now.**

**Update (2026-09-28, `sudo-queue/94` run for real, found a real bug in it)** — the founder ran
`sudo-queue/94-deadweight-wotan-ws-bridge.sh`: `dw-ws-bridge.service` installed and confirmed
running (`systemctl --user status dw-ws-bridge` → active, listening on `127.0.0.1:8765`,
verified proxying live to the real `dw_server` on `:6980`). But `curl .../DEADWEIGHT/ws` still
returned `404` afterward — investigated rather than assumed-fine. Root cause: `94`'s nginx-edit
step inserted the new `location /DEADWEIGHT/ws` block using
`content.rstrip().rfind('}')` — the last closing brace in the *whole file*. certbot's own standard
layout for this vhost appends a **second** `server { listen 80; ... return 301
https://$host$request_uri; }` block after the real HTTPS-serving one, so the location landed
inside that redirect-only block instead. A bare server-level `return 301` fires for every request
before any location inside that same block is ever evaluated, so the block was syntactically valid
(`nginx -t` passed, reload succeeded) but structurally unreachable — explaining exactly the
observed symptom (plain `http://.../DEADWEIGHT/ws` still 301-redirects to https as always; `https`
falls through to `location /`'s `try_files` and 404s, since no location matched it there).
Diagnosed without being able to read the live (root:root, mode 640) nginx file directly, purely
from behavioral evidence (`http` vs `https` response codes, `dw-ws-bridge.service`'s own
independent health, `systemctl` reload/journal timestamps) plus reasoning about certbot's known
nginx-plugin output shape.

**Fix**: `sudo-queue/95-fix-deadweight-ws-nginx-location.sh` (new) — idempotent, anchor-based
instead of rfind-based: removes any existing `/DEADWEIGHT/ws` location block via real
brace-counting (wherever it landed), then re-inserts it immediately after the known-good
`location /api/ { ... }` block's own matching closing brace (that block has been live and
correctly routing since 2026-09-04, so it's a provably-correct anchor for "the real
HTTPS-serving server block"). The removal/insertion logic was unit-tested against a synthetic
fixture reproducing the exact suspected live layout (main HTTPS block + certbot's redirect block,
with the misplaced location inserted into the redirect block exactly as `94`'s logic would have
done) — confirmed it correctly relocates the block and is idempotent (a second run is a no-op
diff). **Not yet run against the real live file** — needs the same sudo this sandbox doesn't
have; queued for the founder/an operator with real sudo, same as `94` was.

**Remaining, real, honest gap**: it still can't actually play yet, for this one specific,
now-understood reason. Until `95` is run for real, the page loads, the wasm module loads, but
clicking Connect/Sign in with IDUNA fails to reach a server. Everything else about the bridge
itself (the systemd unit, the loopback proxy target, `dw_server` on the other end) is confirmed
working — this is purely an nginx routing placement bug, not a bridge or client bug.

**Update (2026-09-28, real brutalist pixel-parity pass)** — founder direction, same session:
"get it pixel for pixel parity with the windows client everything needs to work there first."
Checked directly rather than assumed-fine: the browser client's actual styling (`web/index.html`'s
`<style>` block, `main.ts`'s `KIND_COLORS`, `fx.ts`'s hardcoded animation colors) had **never been
brought into line with `docs/BRAND_STYLE_GUIDE.md`** — it used a generic `system-ui, sans-serif`
font (the guide's single most explicit rule: "never a humanist sans"), `border-radius` on every
input/button/panel (the guide's own named "Do Not Do": "No soft/rounded UI geometry... Hard
rectangles only"), and invented hex colors (`#c0392b`, `#e05252`, `#4a90d9`, etc.) that didn't
match `apps/gui/main.c`'s real `Col`/`KIND_COL` constants or `fx.c`'s own `C3` animation palette
at all.

Fixed for real, not just approximated:
- Every UI color now uses the *exact* hex from `main.c`'s `Col` constants (Terminal Black
  `#12141C`, Corporate Grey `#202432`, Signal White `#EBEBF0`, Dim Grey `#82879B`, Clearance Green
  `#50C878`, Error Red `#E15046`, Cursor White `#FFFFFF`, Locked Grey `#5A5A69`, Access Gold
  `#FFC83C`, and the Offense/Operations/Defense kind triangle).
- Every fx.ts animation color now uses `fx.c`'s own separate, brighter `C3` constants (RED/YEL/
  BLU/CYAN/GRN/ORG/SIL/GRY) for the *same* effect in the *same* scenario, checked by reading
  `fx.c` directly rather than reusing the static-UI palette by assumption (fx.c deliberately uses
  a different, more saturated palette for animation flashes than for menu chrome).
- `border-radius: 0` everywhere; buttons changed from bordered to borderless solid-fill blocks and
  inputs given a visible white 2px frame — both checked against the real reference screenshots
  already in this repo (`docs/img/s513_desktop_menu.png`, `s513_desktop_match.png`), which show
  exactly that split (flat buttons, framed inputs).
- Added Google Fonts "Silkscreen" (a genuine blocky pixel webfont) as the primary font — the
  brand guide explicitly names "a genuine pixel font" as an acceptable substitute for `main.c`'s
  hand-rolled 5x7 bitmap `FONT` table, with a real monospace fallback stack if it fails to load.
- UI-chrome text (headers, labels, buttons, status line) gets `text-transform: uppercase`, scoped
  to exclude card names/keywords/player text, which keep their natural case on the desktop client
  too (checked directly in `main.c`: `dw_card_name()`/`dw_keyword_name()` are never uppercased,
  only literal chrome strings like `"COST %d PWR %d"` and the `KIND_NAME` array are).
- `KIND_COLORS`/`KIND_NAMES` in `main.ts` are now byte-exact to `main.c`'s `KIND_COL`/`KIND_NAME`
  arrays (previously neither matched — different hex, and title-case instead of the desktop's
  all-caps `"OFFENSE"/"OPERATIONS"/"DEFENSE"`).

**Verified for real, not claimed on faith**: `npm run build` clean; a real headless Chrome (this
sandbox already had `ms-playwright`'s `chrome-linux64` binary cached — no install needed) took a
real screenshot of the actual live page at `https://wotan.okemily.com/DEADWEIGHT/` after
redeploying (`~/wotan-deploy.sh` run for real); rendered pixels sampled with ImageMagick
`convert -format "%[pixel:p{x,y}]"` confirmed **exact** byte matches, not just visual similarity:
body background `srgb(18,20,28)` = `#12141C`, warning-box border `srgb(255,200,60)` = `#FFC83C`,
input border `srgb(255,255,255)` = `#FFFFFF`, button fill `srgb(32,36,50)` = `#202432`. The live
page also confirmed `applyProductionDefaults()` is still working correctly post-redeploy (IDUNA
URL field blank/same-origin, bridge URL auto-filled to `wss://wotan.okemily.com/DEADWEIGHT/ws`).

**Real, honest, named remaining gap (as of that pass)**: it was a verified first pass on the
setup/menu screen's chrome only — not yet the in-match HUD/card layout, not yet the title sizing,
and not live-match-tested at all (no headless-Chrome-drivable match was set up).

**Update (2026-09-28, same day, founder real-time: "continue to ensure DEADWEIGHT is pixel for
pixel the windows version")** — closed every gap named above, this time verified against a real,
live, playing match (a throwaway `dw_server --no-auth --fast-forward` + `dw_bot` + WS↔TCP bridge
on private ports, driven end to end with Playwright/headless Chrome — the actual compiled
`dist/main.js`/`dist/client.js`, not a mock):

- **Title**: `<h1>` is now just "DEADWEIGHT" at a large scale (matching `text_c(..., 5, ...,
  "DEADWEIGHT")`'s own huge, left-aligned treatment), with the descriptive "card battler — browser
  client, dev build" text demoted to a small `#subtitle` line below it — the same two-tier
  huge-title/smaller-identity-line hierarchy `docs/img/s513_desktop_menu.png` shows
  ("DEADWEIGHT" then "NODE-CRL5" then "TICKETS: 20/20").
- **In-match HUD**: real hull bars (`.hbar`/`.hbar-fill`), not text — a panel-background bar filled
  proportionally to current/max hull, green above ⅓ hull else red (`hull_bar()`'s own exact rule),
  with the name+fraction on the left and armor/vault (`A0 $4`, or `A? $?` while Merkle-Blindness-
  hidden, checked against the real `armorOpp===255` wire sentinel) on the right inside the same
  bar — for BOTH the opponent and yourself, not just yourself as before. Real energy pip rows
  (`pips()`'s own fixed-6-squares rule, filled yellow up to the current count) for both sides, plus
  the opponent's live hand-card count and a real `ROUND N/MAX` line using `rules.maxRounds()`
  (PARENA-compiled, not a guessed constant) — none of this existed as anything but plain unstyled
  text before.
- **Hand cards**: `card_box()`'s real layout — a solid full-width kind-colored HEADER BLOCK holding
  the name (not a thin colored top border), then Cost/Pwr, kind/keyword, wrapped rules text, and a
  small slot number — in a real 2-column grid matching the reference screenshot's own layout.
- **Real interaction-model parity, not just visual**: checked `apps/gui/main.c`'s `click()`
  directly and found clicking a card (or PASS) never submits anything by itself — it only calls
  `select_slot()`; only LOCK IN (`lock_selected()`) actually sends `DW_C_PLAY`. The browser client
  previously submitted immediately on card click, a real functional deviation, not just cosmetic.
  Fixed: clicking a card now only selects it (white frame, matching `state==1`'s selection frame),
  LOCK IN enables and turns Clearance-Green-accented (matching `button(...,"LOCK IN",C_GOOD,...)`)
  only once something is selected, and PASS's own label changes to "PASS *"/"PASSED" exactly like
  `A.sel==-1 ? (locked?"PASSED":"PASS *") : "PASS"`. `onPlayReject`'s reset behavior also now
  matches `main.c` exactly: `DW_REJ_ALREADY_LOCKED` (4) leaves selection state alone, any other
  reason clears it.
- **Real found-and-fixed bug, not new work**: `fx.ts`'s `drawShip()` filled the ship polygon with
  `#202432` (C_PANEL, the UI chrome background) — the real `apps/gui/fx.c` fills it with a
  separate, fx-specific color, `DGR` = `{60,64,78}` = `#3C404E`, deliberately distinct from the
  static-UI panel color (same "fx.c uses its own brighter/distinct palette" finding the prior pass
  already made for the clash-effect colors). Fixed.
- **Real correction to the prior pass's own "gap" note**: re-checked `fx.c`'s `draw_ship()`/
  `ship_poly()` directly rather than trusting the earlier assumption — the desktop client does
  NOT draw a different ship silhouette per kind, only a different accent color (`acc[s] =
  KIND3[cardk[s]]`) on the SAME fixed 11-point polygon. `fx.ts`'s `drawShip()` already uses that
  exact same 11-point polygon (`(34,0),(6,-10),(-6,-22),...` — byte-identical coordinates). This
  was never a real gap; the prior pass's note was simply wrong, corrected here rather than left
  standing.
- **Verified live, pixel-sampled, not just visually similar**: a real match played end to end
  through the actual compiled client (queue → MATCH_FOUND → ROUND_START → select a card → LOCK IN)
  against a real bot. ImageMagick-sampled exact hex matches: hull bar fill `srgb(80,200,120)` =
  `#50C878` (Clearance Green), Offense card header `srgb(215,70,60)` = `#D7463C`, Operations card
  header `srgb(225,160,40)` = `#E1A028`, Defense card header `srgb(70,140,230)` = `#468CE6`, LOCK
  IN's enabled-state fill `srgb(80,200,120)` = `#50C878` — every one a byte-exact match to
  `main.c`'s own constants, not an approximation.
- **Real, honest, still-not-covered gap**: Silkscreen remains a genuine pixel font, not a literal
  reproduction of `main.c`'s specific 5x7 glyph table (the brand guide itself calls this an
  acceptable substitute — not treated as a gap). The round-log line still uses `#log`'s own
  scrolling multi-line history rather than reproducing the desktop's single most-recent-round
  summary line (`R1 YOU -3 +0 OPP -0 +0`) — a real, deliberate, named simplification (the scrolling
  log is strictly more informative, not a downgrade) rather than an oversight. Draft mode still has
  no browser UI at all (`web/README.md`'s own long-standing honest gap, unrelated to this pass).

## Update (2026-09-28, real production matchmaking bugfix)

Founder real-time: "we dont need IDUNA base URL or WebSocket bridge URL - we arent setting up for
multi server right this second its just the one server - also i dunno if its the wrong url or what
but the actual game still doesnt work - hitting connect then random should get me into a game" +
"dont make it a dev build this is production" + `/design` "make DEADWEIGHT affordances nice
keeping the art direction... elite... hacker aesthetic".

**Production hardening**: removed the `iduna-url`/`bridge-url` text inputs from `index.html`
entirely — this is a single production server, not a multi-server setup, so both endpoints are now
hardcoded in `main.ts` (`resolveIdunaUrl`/`resolveBridgeUrl`: same-origin `/api/` + `/DEADWEIGHT/ws`
in production, `localhost:8080`/`:8765` only as a local-dev fallback). Removed the dev-build
subtitle and the "VS0.5-web, not a shipped client" disclaimer banner, replaced with the real brand
tagline "Dark Sector: Hold Battles". Added the brand guide's own Section 2A motifs that were named
but never actually built: a full-screen scanline/CRT post-process, dashed borders for pending
friend/duel state, a blinking terminal caret — deliberately no drop shadows, glows, or gradients on
chrome, which that same doc's Do-Not-Do list rules out.

**The real bug**: testing the live matchmaking flow against the actual production stack (not a
`--no-auth` throwaway server, which is what every prior verification pass in this repo's history —
including this session's own — had used) surfaced the real cause of "connect then random doesnt
get me into a game": `HELLO`'s inline token field is capped at 200 bytes (`DW_MAX_TOKEN`,
`core/protocol.h`), but a real IDUNA ES256 player JWT is ~400-500 bytes. `client.ts` stuffed the
full token into `encodeHello()` regardless, silently truncating it to garbage; the real, auth-
required production `dw_server` correctly rejected the mangled token with `ERROR 2` (auth) and
closed the connection before ever sending `WELCOME`. The wire protocol already names the right fix
(`docs/WIRE_PROTOCOL.md`'s AUTH row: "with auth required the client sends HELLO with token_len = 0
and then AUTH", `DW_MAX_AUTH_TOKEN = 900`) and the C/wasm side already exported the hooks for it
(`set_auth`/`set_auth_token_byte` in `apps/wasm/protocol_wasm.c`), but neither TypeScript codec —
not `proto.ts` (the original hand-written port) and not its wasm-backed drop-in `wasmProto.ts` —
ever called it. This bug predates the wasm rewrite; it is a real, long-standing gap in the entire
browser client's auth handling, only now surfaced by finally testing against real auth.

**Fix**: added `encodeAuth(token)` to both `proto.ts` and `wasmProto.ts` (u16 length, up to 900
bytes); `client.ts`'s `connect()` now sends `HELLO` with an empty inline token and, whenever a real
token exists, immediately follows with a separate `AUTH` frame. Added a byte-parity check between
the two codecs using a realistic 557-byte JWT fixture (`test_wasm_proto_parity.mjs`, now 15/15).

**Verified against real production, not a throwaway**: minted a real guest JWT via the live
`https://wotan.okemily.com/api/v1/games/deadweight/guest-register` endpoint, connected
`DeadweightClient` directly to `wss://wotan.okemily.com/DEADWEIGHT/ws` — got a real
`WELCOME(authRequired=true)` → `AUTH` → `QUEUED` → real `MATCH_FOUND` against a live `bot-ripper`
from the actual production bot pool. Also independently confirmed (already fixed live between
checks, not by this session's own work) that the `/DEADWEIGHT/ws` nginx 404 tracked above is
resolved: `curl` now returns a real `101 Switching Protocols`.

## What's NOT built yet — real, phased, not glossed over

1. ~~**Rendering.**~~ Done above — `web/src/fx.ts`/`main.ts`'s existing brutalist Canvas2D
   renderer now runs on the wasm-backed codec via `client.ts`'s import swap; no renderer code
   itself needed to change, since `wasmProto.ts` exports the identical `ServerFrame` shapes
   `fx.ts`/`main.ts` already consumed. `card_rules.prn`/`fx_rules.prn` still reach the browser via
   PARENA's own TypeScript emitter (`web/src/generated/CardRules.ts`/`FxRules.ts`), untouched.
2. ~~**Networking.**~~ Done above — `web/bridge/ws-tcp-bridge.js` (unmodified) relays the wasm
   module's encoded bytes exactly as it always relayed `proto.ts`'s.
3. ~~**IDUNA SSO.**~~ Done above — `sso.ts` + `account.ts`'s `loginWithSso` reuse WOTAN's exact
   flow (in the JS host, not in wasm — auth/HTTP has no business inside the compute module).
4. ~~**`wotan.okemily.com/DEADWEIGHT` hosting.**~~ **Live and playable.** The static page is
   genuinely reachable at `https://wotan.okemily.com/DEADWEIGHT/`. `/DEADWEIGHT/ws` is live (a real
   `101 Switching Protocols`, not `404` — the nginx location bug `95` targeted is fixed). A
   separate, real client-side bug (found 2026-09-28 testing against production for the first
   time: HELLO's 200-byte inline token field was silently truncating every real ~400-500 byte
   IDUNA JWT, so the auth-required production `dw_server` rejected every real-auth connection with
   `ERROR 2` before `WELCOME` — see "Update (2026-09-28, real production matchmaking bugfix)"
   below) is also fixed. Connect → Queue (random) now genuinely pairs a real session against the
   live bot pool/other players, verified end to end against production. The separate, parallel
   `docs/WASM_DEPLOY_NORTHSTAR.md` GKE/Terraform pipeline (a different, k8s-based hosting target
   for the same eventual artifact) is real but currently blocked on a non-functional GKE cluster —
   `wotan.okemily.com` is the simpler, already-live path and already shipped first.
5. **Android parity (Phase 1C/3/4).** Tracked separately, `docs/ANDROID_PARITY_NORTHSTAR.md` —
   unrelated to the wasm work beyond sharing the same founder ask's framing ("parity... all
   platforms").

## Why this is a real, honest V0 slice and not the whole ask

"Eat the codebase... should look the same in all platforms" is a large, multi-session initiative.
This pass proves the *hardest structurally uncertain part* — that a real, native, Emscripten-free
wasm32 build of DEADWEIGHT's actual hand-written C is possible at all — and then carries it all the
way through: wire codec, rendering, networking, IDUNA SSO, and a real, live page at
`https://wotan.okemily.com/DEADWEIGHT/` (curl-verified 200s), linked from WOTAN's own nav, same
session. What's left is narrow and named, not glossed over: it can't play a match yet, since a
real nginx routing bug found in `sudo-queue/94` (item 4 above) needs its fix
(`sudo-queue/95-fix-deadweight-ws-nginx-location.sh`) run with sudo this sandbox doesn't have, and
Android parity (item 5) is real, separate, tracked work, not part of this doc's scope.
