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

## Honest status / limits — not built yet

- **Random queue only.** Draft mode's own `DRAFT_OFFER`/`DRAFT_PICK`/`DRAFT_DONE` frames are typed
  in `proto.ts` but `main.ts`'s UI never sends a draft pick — draft is real everywhere else
  (server, bots, Windows GUI, Android) but not wired into this client.
- **No IDUNA auth.** Connects with `HELLO`'s empty inline token, matching `--no-auth` servers only
  (same precedent the Windows GUI/Android clients document). AUTH (`0x06`, real IDUNA JWTs) is
  typed in `proto.ts`'s constants but never sent.
- **No animation or audio.** The Windows SDL2 client's clash scenes (`docs/ANIMATION_AND_AUDIO.md`)
  are not ported here — round results are plain text log lines.
- **No reconnect/resume.** A dropped WebSocket just ends the session; no session-id-based rejoin.
- **Not deployed anywhere.** This is a local-only dev build (`python3 -m http.server`); no public
  URL, no TLS, no production `dw_server` pointed at it.
- **The WS↔TCP bridge is a single-process relay**, not hardened for public exposure (no rate
  limiting, no origin checks) — fine for local dev, not for pointing at the internet as-is.
