# DEADWEIGHT — browser client (VS0.5-web, dev)

Real, working, but not a shipped client. A live proof that PARENA's TypeScript emitter
(`PARENA/src/emit_ts.c`) can drive a browser client for the real card battler over the real wire
protocol, against a real `dw_server` — verified end-to-end (see "Verified" below), not just
compiled. Founder real-time (2026-09-21): "build out DEADWEIGHT in the browser, use PARENA" →
"DOGFOOD THE TS WASM SHIT."

## What's here

- `src/generated/CardRules.ts` — `PARENA/stdlib/deadweight/card_rules.prn` compiled straight to
  TypeScript (`scripts/gen_rules.sh` now emits this alongside the existing C/Java targets — one
  rules source, three targets, zero FFI, same discipline the root `README.md` documents). The
  browser calls `isLegalPlay`/`cardKind`/`cardKeyword`/etc. directly; nothing here re-implements
  card logic by hand.
- `src/generated/cards.json` — the full 105-card catalog (name/kind/keyword/cost/power/text),
  from the existing `scripts/export_cards.sh` generator (the same one the WOTAN deck browser
  already reads).
- `src/proto.ts` — a hand-written TypeScript port of `docs/WIRE_PROTOCOL.md`'s byte layout (that
  doc is the source of truth; this is not generated).
- `src/client.ts` — the game state machine (WebSocket → decoded frames → typed events).
- `src/main.ts` + `index.html` — the UI: connect, queue (random mode only), see your hand rendered
  from the real card data, lock in a play or pass, watch round results.
- `bridge/ws-tcp-bridge.js` — a **dumb, protocol-agnostic** WebSocket↔TCP relay. Browsers can't
  open raw TCP sockets; `dw_server` only speaks raw TCP (and stays that way — this bridge doesn't
  know or care about the wire protocol, it just copies bytes). The real protocol is implemented
  exactly twice in this whole system: once in `core/` (C, authoritative) and once in `src/proto.ts`
  (browser) — the bridge is not a third copy.
- `src/generated/FxRules.ts` + `src/fx.ts` — round-resolution animation/audio. `FxRules.ts` is
  `PARENA/stdlib/deadweight/fx_rules.prn` compiled to TypeScript — the exact same scenario/winner/
  critical/timeline/audio-cue decision logic `apps/gui/fx.c` (Windows, canonical) now calls too,
  instead of the two hand-deriving it independently. `fx.ts` calls those functions and then draws
  with Canvas2D and plays cues with Web Audio oscillators — a real, different renderer making the
  *same decisions*, not a pixel/sample clone of the SDL2 client (see `docs/ANIMATION_AND_AUDIO.md`
  for the full "what's shared vs. what's hand-written per platform" breakdown).

## Running it locally

```bash
# 1. rules + catalog (only needed after a card_rules.prn or card table change)
cd /home/fatbaby/DEADWEIGHT && scripts/gen_rules.sh && scripts/export_cards.sh web/src/generated/cards.json

# 2. compile TypeScript
cd web && npm install --save-dev typescript && ./node_modules/.bin/tsc -p tsconfig.json
cp src/generated/cards.json dist/generated/cards.json   # tsc doesn't copy non-.ts assets

# 3. a no-auth dw_server to play against (use a DIFFERENT port from any already-running instance)
cd .. && ./build/dw_server --port 7900 --no-auth --fast-forward &
./build/dw_bot --archetype wall --host 127.0.0.1 --port 7900 --name bot-test --matches 5 &

# 4. the WS<->TCP bridge
cd web/bridge && npm install && node ws-tcp-bridge.js --ws-port 8765 --tcp-port 7900 &

# 5. serve the static page and open it
cd .. && python3 -m http.server 8080
# open http://localhost:8080/ , bridge URL defaults to ws://localhost:8765
```

## Verified (2026-09-21)

Not just "it compiles" — a real, live, end-to-end match:

- `web/bridge/e2e_test.mjs` drives the **actual compiled browser client** (`dist/client.js`,
  `dist/proto.js`, `dist/generated/CardRules.js` — the exact code the browser loads, via Node's
  `ws` package standing in for the browser's native `WebSocket`) through a full match against a
  live `dw_bot`, over the real bridge, against a real `dw_server`. Connected, queued, matched,
  played rounds picking cards with the real PARENA-compiled `isLegalPlay`, reached `MATCH_END`.
  11 rounds played, 0 protocol errors.
- Two real, load-bearing bugs found in PARENA's TypeScript emitter along the way (not toy issues —
  both would have silently corrupted the compiled card catalog) — see `PARENA/STDLIB.md`'s own
  "TypeScript emitter" section for the full writeup:
  1. `I32`/`I32` division lowered to TypeScript's real-number `/` instead of truncating toward
     zero like C/Java's `/` — `card_rules.prn`'s packed-bitfield card lookups depend on
     truncation. Fixed; verified against all 2808 `fx-amount` parity vectors, 0 mismatches.
  2. A fixed 512-byte format buffer silently truncated any single expression over 511 characters
     (the exact same bug already found and fixed in the Java emitter, never ported to TypeScript)
     — `card_rules.prn`'s deeply nested `fx-amount` chains hit this routinely, producing
     syntactically corrupted output that still "succeeded." Fixed.
  3. `(not x)` fell through into a bogus call to a never-defined `not(...)` function — the same
     gap already found and fixed in the C and Java emitters and in BURROW. Fixed.
  4. `true`/`false` literals weren't recognized at all (a real, latent type-tracking bug, found
     writing `fx_rules.prn`'s own `fx-is-crit`) — fixed.
- **Round-resolution animation/audio** (2026-09-21, "use parena to unify the windows and TS
  versions... dogfood it, eat more of the app"): `fx.computeTimeline()` calls the exact same
  PARENA-compiled scenario/timeline logic Windows now calls too (`FxRules.ts`/`core/fx_rules.c`,
  both from `fx_rules.prn`) — verified sane against 9 real rounds of a real live match
  (`web/bridge/e2e_test.mjs`), and cross-verified correct via `tests/test_fx_rules.c`'s 4092-check
  independent oracle on the C side (same compiled logic). The Canvas2D/Web Audio *rendering* of
  those decisions has not been checked in a real browser window (Node has no DOM) — see below.

## Honest status / limits — not built yet

- **Random queue only.** Draft mode's own `DRAFT_OFFER`/`DRAFT_PICK`/`DRAFT_DONE` frames are typed
  in `proto.ts` but `main.ts`'s UI never sends a draft pick — draft is real everywhere else
  (server, bots, Windows GUI, Android) but not wired into this client.
- **IDUNA auth (S537, 2026-09-24): real, wired, partially verified.** `src/account.ts` mirrors
  `apps/gui/main.c`'s own shipped account flow: auto guest-register on first load, a persisted
  `guest_secret` for silent re-login (`guest-login`) on return visits, and a "Link email" form
  (`guest-upgrade`) that claims the guest account in place -- the same identity WOTAN's
  `friends.html`/`profile.html` operate on. `client.connect()` now takes a real token and passes
  it into `HELLO` (previously always `''`). Live-verified against the real, running IDUNA
  (127.0.0.1:8080): `bootstrapAccount()` mints a real guest account and a second call correctly
  reuses the same `player_id` via `guest-login` (not a fresh register) once a browser-shaped
  `localStorage` is present. **Not verified this pass**: an actual `dw_server` (with `--iduna-url`
  set, i.e. auth required, not `--no-auth`) accepting a real web-client token over `HELLO` --
  IDUNA's real per-IP daily signup cap (3/day, by design) was hit by this session's own repeated
  test registrations before that check could run, and bypassing it (even locally, even for
  testing) was correctly refused as a security-weakening action. `encodeHello`'s wire format
  itself is unchanged, pre-existing code already proven against real tokens by the native
  clients -- the residual risk is narrow, but this specific path is code-reviewed, not live-run.
  Re-run once the cap clears (~24h) or from a different source IP. **Still missing**: no CORS
  headers on IDUNA's `/api/v1/games/` routes. A browser treats `localhost:8791` (this static
  site) and `localhost:8080` (IDUNA) as different origins even on the same machine, so a real
  browser run of this page needs either a same-origin reverse proxy (WOTAN's own
  `ops/nginx-wotan.conf` `/api/` pattern) or IDUNA-side CORS support -- neither exists yet. The
  live verification above used Node's `fetch` directly (not a browser), which isn't
  CORS-restricted, so the account-bootstrap logic is proven but a real browser tab hitting this
  same flow is not yet.
- **Animation/audio rendering not verified in a real browser.** `fx.ts`'s Canvas2D drawing and Web
  Audio playback have never been opened in an actual browser window in this sandbox (no display) —
  only the shared decision layer they're driven by has been checked live (see above). The visuals
  are also a real, simplified re-interpretation of the Windows client's own scenes (ship polygons,
  a clash effect per scenario family, hull/armor number pop-ups), not a port of its exact particle
  system, shake, crack-glass, or status (burn/regen/EMP) effects — those are not built here yet.
- **Energy-delta and burn/regen status visuals/audio are a named, not-yet-wired gap**, honestly,
  not silently skipped: `docs/WIRE_PROTOCOL.md`'s `ROUND_RESULT` carries no energy-delta or
  status-after field (status is only ever visible on the *next* `ROUND_START`), so `main.ts`
  currently always passes `energyDeltaYou/Opp: 0` and `newStatusYou/Opp: false` into
  `fx.computeTimeline()` — the `hasEcon`/`hasStat` stages still fire correctly for credits/armor
  and for the disabled/swapped flags that *are* in `ROUND_RESULT`, just not for energy gain or a
  burn/regen status literally starting this round.
- **No reconnect/resume.** A dropped WebSocket just ends the session; no session-id-based rejoin.
- **Not deployed anywhere.** This is a local-only dev build (`python3 -m http.server`); no public
  URL, no TLS, no production `dw_server` pointed at it.
- **The WS↔TCP bridge is a single-process relay**, not hardened for public exposure (no rate
  limiting, no origin checks) — fine for local dev, not for pointing at the internet as-is.
