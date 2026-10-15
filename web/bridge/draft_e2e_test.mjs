// Headless end-to-end test of the browser client's DRAFT mode (kanban #626): the REAL compiled client
// (dist/client.js + dist/wasmProto.js) connects with HELLO mode 2 through the real ws-tcp-bridge to a real
// dw_server, queues, takes all 16 DRAFT_OFFER picks (1x/2x/3x as buckets allow), and must receive DRAFT_DONE
// with a full 23-card deck followed by QUEUED. Then RESUME: a second connection submits that deck with
// DRAFT_RESUME and must get the same deck echoed back as DRAFT_DONE.
import { DeadweightClient } from '../dist/client.js';
import { initWasmProto } from '../dist/wasmProto.js';
import { WebSocket } from 'ws';
import { readFile } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

globalThis.WebSocket = WebSocket;
const wasmPath = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'dist', 'generated', 'dw_protocol.wasm');
globalThis.fetch = async () => ({
    arrayBuffer: () => new Promise((res, rej) => readFile(wasmPath, (e, d) => e ? rej(e) : res(d.buffer.slice(d.byteOffset, d.byteOffset + d.byteLength)))),
});
await initWasmProto();

const URL = process.argv[2] || 'ws://localhost:8765';
let fails = 0;
const check = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) fails++; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

function run(mode, onReady) {
    return new Promise((resolve) => {
        const out = { offers: 0, done: null, queued: false, errors: [] };
        const c = new DeadweightClient(URL, {
            onState: (s) => { if (s === 'ready') onReady(c); },
            onDraftOffer: (f) => {
                out.offers++;
                let m = 1; while (m < 3 && f.left[m - 1] <= 0) m++;
                c.draftPick(0, m);
            },
            onDraftDone: (f) => { out.done = f; out.doneAt = out.offers; setTimeout(() => { c.close(); resolve(out); }, 300); },
            onQueued: () => { out.queued = true; },
            onError: (f) => { out.errors.push(f.code); },
        });
        c.connect('DraftTest', '', mode);
        setTimeout(() => { c.close(); resolve(out); }, 8000);
    });
}

const a = await run(2, (c) => c.queue(0));
check(a.errors.length === 0, 'no server errors during the draft');
check(a.offers >= 16, `received the 16 draft offers (got ${a.offers})`);
check(a.done && a.done.cards.length === 23, 'DRAFT_DONE carries a 23-card deck');
check(a.done && a.done.cards.every((x) => x >= 0), 'every deck slot is a real card');
check(a.queued, 'server queued the drafted deck');

if (a.done) {
    const deck = a.done.cards.slice();
    let echoed = null;
    await new Promise((resolve) => {
        const c = new DeadweightClient(URL, {
            onState: (s) => { if (s === 'ready') c.draftResume(deck); },
            onDraftDone: (f) => { echoed = f; setTimeout(() => { c.close(); resolve(); }, 200); },
        });
        c.connect('DraftTest', '', 2);
        setTimeout(() => { c.close(); resolve(); }, 6000);
    });
    check(echoed && JSON.stringify(echoed.cards) === JSON.stringify(deck), 'DRAFT_RESUME echoes the same deck back');
}
console.log(fails ? `FAILED (${fails})` : 'PASSED');
process.exit(fails ? 1 : 0);
