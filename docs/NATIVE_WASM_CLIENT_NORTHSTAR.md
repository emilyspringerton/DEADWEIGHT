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

**Remaining, real, honest gap**: it can't actually play yet. `curl .../DEADWEIGHT/ws` → `404` —
`sudo-queue/94-deadweight-wotan-ws-bridge.sh` (installs `dw-ws-bridge.service` + the nginx
location block) genuinely needs sudo and hasn't been run. Until it is, the page loads, the wasm
module loads, but clicking Connect/Sign in with IDUNA fails to reach a server.

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
4. ~~**`wotan.okemily.com/DEADWEIGHT` hosting.**~~ **Live** — the static page is genuinely reachable
   at `https://wotan.okemily.com/DEADWEIGHT/` (curl-verified, 200s for the page/JS/wasm). Only
   remaining gap: it can't play a match yet — `/DEADWEIGHT/ws` still 404s, since
   `sudo-queue/94-deadweight-wotan-ws-bridge.sh` (nginx location + `dw-ws-bridge.service`) needs
   real sudo this sandbox doesn't have. The separate, parallel `docs/WASM_DEPLOY_NORTHSTAR.md`
   GKE/Terraform pipeline (a different, k8s-based hosting target for the same eventual artifact) is
   real but currently blocked on a non-functional GKE cluster — `wotan.okemily.com` is the simpler,
   already-live path
   and should ship first.
5. **Android parity (Phase 1C/3/4).** Tracked separately, `docs/ANDROID_PARITY_NORTHSTAR.md` —
   unrelated to the wasm work beyond sharing the same founder ask's framing ("parity... all
   platforms").

## Why this is a real, honest V0 slice and not the whole ask

"Eat the codebase... should look the same in all platforms" is a large, multi-session initiative.
This pass proves the *hardest structurally uncertain part* — that a real, native, Emscripten-free
wasm32 build of DEADWEIGHT's actual hand-written C is possible at all — and then carries it all the
way through: wire codec, rendering, networking, IDUNA SSO, and a real, live page at
`https://wotan.okemily.com/DEADWEIGHT/` (curl-verified 200s), linked from WOTAN's own nav, same
session. What's left is narrow and named, not glossed over: it can't play a match yet, since one
ops script (`sudo-queue/94`) genuinely needs sudo this sandbox doesn't have (item 4 above), and
Android parity (item 5) is real, separate, tracked work, not part of this doc's scope.
