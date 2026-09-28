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
loudly if either binary or the build itself is missing). Output: `web-wasm/generated/dw_protocol.wasm`
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

## What's NOT built yet — real, phased, not glossed over

1. **Rendering.** No wasm module in this plan ever draws pixels — matching MIXFORGE's own real
   split (its wasm is pure compute/DSP; Canvas2D/Web Audio rendering is hand-written JS in
   `engine.mjs`/`multiplayer.html`). The natural next step is extending the *existing*
   `web/src/fx.ts`/`main.ts` brutalist Canvas2D renderer (already built, already styled, per
   `docs/ANIMATION_AND_AUDIO.md`) to call this wasm module's protocol codec instead of
   `web/src/proto.ts`'s hand-port — replacing one specific duplicated layer, not rewriting the
   whole browser client. `card_rules.prn`/`fx_rules.prn` already reach the browser via PARENA's
   own TypeScript emitter (`web/src/generated/CardRules.ts`/`FxRules.ts`) — that path is untouched
   and already real; native wasm is additive for the parts that had no shared-logic story at all
   (the protocol codec, and potentially `core/draft.c`/`core/policy.c` next).
2. **Networking.** `web/bridge/ws-tcp-bridge.js` (real, live, already used by the existing TS
   client) is the reusable relay for wasm too — the wasm module never touches sockets itself, it
   only encodes/decodes bytes that JS sends/receives over a plain `WebSocket`. Not wired yet.
3. **IDUNA SSO.** WOTAN already has a real, live, end-to-end-verified pattern for exactly this
   (`WOTAN/friends.html`/`store.html`): redirect to `https://iam.okemily.com/?redirect_uri=<page>`,
   receive a token via URL fragment, then `POST /api/v1/games/deadweight/sso-exchange` (real,
   live, `IDUNA/internal/http/handlers/game_online.go`) to mint a real DEADWEIGHT player token.
   The native wasm client should reuse this exact flow (in the JS host, not in wasm — auth/HTTP
   has no business inside the compute module) rather than the existing `web/src/account.ts`'s own
   separate guest-only bootstrap. Not wired yet.
4. **`wotan.okemily.com/DEADWEIGHT` hosting.** Real static hosting target once the above land;
   `WOTAN`'s own deploy pipeline (plain static HTML/CSS/JS, no build step, `~/wotan-deploy.sh`)
   already fits a "wasm + a small JS host + static HTML" artifact directly. The separate,
   parallel `docs/WASM_DEPLOY_NORTHSTAR.md` GKE/Terraform pipeline (a different, k8s-based hosting
   target for the same eventual artifact) is real but currently blocked on a non-functional GKE
   cluster — `wotan.okemily.com` is the simpler, already-live path and should probably ship first.
5. **Android parity (Phase 1C/3/4).** Tracked separately, `docs/ANDROID_PARITY_NORTHSTAR.md` —
   unrelated to the wasm work beyond sharing the same founder ask's framing ("parity... all
   platforms").

## Why this is a real, honest V0 slice and not the whole ask

"Eat the codebase... should look the same in all platforms" is a large, multi-session initiative.
This pass proves the *hardest structurally uncertain part* — that a real, native, Emscripten-free
wasm32 build of DEADWEIGHT's actual hand-written C is possible at all, and ships one complete,
verified vertical slice of it (the wire protocol codec, the one piece of client logic that
previously had zero shared-code story across platforms). Rendering, networking, and SSO are real,
named, unstarted follow-ups — not silently deferred.
