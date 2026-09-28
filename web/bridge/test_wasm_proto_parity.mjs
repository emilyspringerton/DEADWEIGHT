// test_wasm_proto_parity.mjs -- real, live proof that wasmProto.ts (the native-wasm-backed
// drop-in for proto.ts) produces byte-identical output to the existing hand-written TypeScript
// codec, and decodes the same bytes back to identical objects. This is the actual "drop-in
// replacement" claim in docs/NATIVE_WASM_CLIENT_NORTHSTAR.md, checked, not just asserted.
//
// Run from web/: node bridge/test_wasm_proto_parity.mjs (needs `npm run build` run first, and
// scripts/build_wasm_native.sh run first to produce dist/generated/dw_protocol.wasm).
import assert from 'node:assert/strict';
import * as proto from '../dist/proto.js';
import * as wasmProto from '../dist/wasmProto.js';

// wasmProto.ts calls global fetch() to load the .wasm -- Node has fetch built in (>=18) but it
// only understands http(s)/data URLs, not bare relative file paths, so shim it here to read
// straight off disk. The browser itself uses a real fetch() against a real relative URL --
// this shim exists only because this test runs under Node, not because the module needs it.
import { readFile } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const here = path.dirname(fileURLToPath(import.meta.url));
const wasmPath = path.join(here, '..', 'dist', 'generated', 'dw_protocol.wasm');
globalThis.fetch = async () => ({
  arrayBuffer: () => new Promise((resolve, reject) => readFile(wasmPath, (err, data) => err ? reject(err) : resolve(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength)))),
});

await wasmProto.initWasmProto();

let checks = 0;
function eqBytes(a, b, name) {
  checks++;
  assert.deepEqual([...a], [...b], `${name}: byte mismatch\n  proto.ts:     ${[...a]}\n  wasmProto.ts: ${[...b]}`);
  console.log(`OK: ${name} (${a.length} bytes, identical)`);
}
function eqObj(a, b, name) {
  checks++;
  assert.deepEqual(a, b, `${name}: decode mismatch\n  proto.ts:     ${JSON.stringify(a)}\n  wasmProto.ts: ${JSON.stringify(b)}`);
  console.log(`OK: ${name} decode identical`);
}

// ---- encode parity: same inputs, same wire bytes ----
eqBytes(proto.encodeHello(2, 0, 'Ripper', 'abc'), wasmProto.encodeHello(2, 0, 'Ripper', 'abc'), 'encodeHello');
eqBytes(proto.encodeHello(0, 1, 'x'.repeat(20), ''), wasmProto.encodeHello(0, 1, 'x'.repeat(20), ''), 'encodeHello (long name, truncates to 16)');
eqBytes(proto.encodeQueue(), wasmProto.encodeQueue(), 'encodeQueue (no args)');
eqBytes(proto.encodeQueue(1), wasmProto.encodeQueue(1), 'encodeQueue (sameDeck=1)');
eqBytes(proto.encodeQueue(0, 'deadbeefdeadbeefdeadbeefdeadbee'), wasmProto.encodeQueue(0, 'deadbeefdeadbeefdeadbeefdeadbee'), 'encodeQueue (with match_token)');
eqBytes(proto.encodePlay(0xdeadbeef, 5, -1), wasmProto.encodePlay(0xdeadbeef, 5, -1), 'encodePlay (pass)');
eqBytes(proto.encodePlay(1, 0, 2), wasmProto.encodePlay(1, 0, 2), 'encodePlay (real slot)');
eqBytes(proto.encodeLeave(), wasmProto.encodeLeave(), 'encodeLeave');
eqBytes(proto.encodePing(424242), wasmProto.encodePing(424242), 'encodePing');

// ---- decode parity: feed proto.ts's own encoded bytes into wasmProto's decoder (cross-check,
// not just "both decode what they each encoded") ----
function serverFrame(type, payload) {
  return Uint8Array.from([type, ...payload]);
}
function u32le(v) { return [v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255]; }

const welcome = serverFrame(proto.ServerMsg.WELCOME, [...u32le(12345), 3]);
eqObj(proto.decodeServerFrame(welcome), wasmProto.decodeServerFrame(welcome), 'WELCOME');

const nameBytes = Array.from({ length: 16 }, (_, i) => 'Bot1'.charCodeAt(i) || 0);
const matchFound = serverFrame(proto.ServerMsg.MATCH_FOUND, [...u32le(100), ...u32le(200), 1, ...nameBytes, 1]);
eqObj(proto.decodeServerFrame(matchFound), wasmProto.decodeServerFrame(matchFound), 'MATCH_FOUND');

const matchEnd = serverFrame(proto.ServerMsg.MATCH_END, [...u32le(55), 1, 0]);
eqObj(proto.decodeServerFrame(matchEnd), wasmProto.decodeServerFrame(matchEnd), 'MATCH_END');

const errorMsg = serverFrame(proto.ServerMsg.ERROR, [2]);
eqObj(proto.decodeServerFrame(errorMsg), wasmProto.decodeServerFrame(errorMsg), 'ERROR');

const roundStart = serverFrame(proto.ServerMsg.ROUND_START, [
  3, /*round*/ 15, 0xf0 /*hullYou=15,hullOpp=-16*/, 8, 6, /*energy*/
  1, 0xff, 2, 0xff, /*hand: 1,-1,2,-1*/ 1, /*oppHandSize*/ 0x88, 0x13, /*deadlineMs=5000*/
  10, 12, /*armor*/ 0xfb, 5, /*vault: -5, 5*/ 0b101, /*lockMask*/ 1, 2, /*status*/
]);
eqObj(proto.decodeServerFrame(roundStart), wasmProto.decodeServerFrame(roundStart), 'ROUND_START');

console.log(`\nPASS: ${checks} checks -- wasmProto.ts (native wasm32, no Emscripten) is byte-and-object-identical to proto.ts for every case checked.`);
