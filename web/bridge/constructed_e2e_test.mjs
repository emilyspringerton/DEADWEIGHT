// Headless end-to-end test of the browser client's CONSTRUCTED mode: the REAL compiled client (dist/client.js + dist/wasmProto.js)
// connects with HELLO mode 3 through the real ws-tcp-bridge to a real dw_server that has a constructed-queue bot waiting.
//   - QUEUE before any deck was submitted                -> ERROR 4 (nothing to queue with)
//   - DRAFT_RESUME with an illegal deck                  -> ERROR 3 (the server checks the deck itself), and the connection survives
//   - DRAFT_RESUME with a legal deck                     -> DRAFT_DONE echoes exactly that deck
//   - QUEUE(same deck)                                   -> QUEUED, then MATCH_FOUND against the bot, and a whole match is played
//   - every card of every opening hand was in the submitted deck (the built deck is what is played, not a random one; later hands can
//     hold the bot's cards after a Realm Warp, which swaps hands)
//   - queueing again on the same connection plays the same deck again
// Run (see web/README.md): build/dw_server --port 7900 --no-auth --fast-forward & ; scripts/bot_pool.sh start --mode constructed --port 7900
//   & node bridge/ws-tcp-bridge.js --ws-port 8765 --tcp-port 7900 & ; node bridge/constructed_e2e_test.mjs [ws://localhost:8765]
import { DeadweightClient } from '../dist/client.js';
import { initWasmProto } from '../dist/wasmProto.js';
import * as rules from '../dist/generated/CardRules.js';
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

// a legal deck: ten singles, five pairs, one triple (16 distinct cards), ids the catalog has
const ids = Array.from({ length: 16 }, (_, i) => i * 3 + 1);
const deck = [];
ids.slice(0, 10).forEach((c) => deck.push(c));
ids.slice(10, 15).forEach((c) => deck.push(c, c));
deck.push(ids[15], ids[15], ids[15]);
const inDeck = new Set(deck);
check(deck.length === 23, 'the test deck has 23 cards');

// one connection with its own event log (dw_server closes a connection after ANY error, so each refusal gets its own)
async function open() {
    const ev = { errors: [], done: [], queued: 0, found: [], ends: 0, handsOutside: 0, hands: 0, rounds: 0, closed: false };
    let ready = false;
    const c = new DeadweightClient(URL, {
        onState: (s) => { if (s === 'ready') ready = true; if (s === 'closed') ev.closed = true; },
        onError: (f) => ev.errors.push(f.code),
        onDraftDone: (f) => ev.done.push(f),
        onQueued: () => { ev.queued++; },
        onMatchFound: (f) => ev.found.push(f),
        onRoundStart: (f) => {
            ev.rounds++;
            let slot = -1;
            for (let i = 0; i < 4; i++) {
                const id = f.hand[i];
                if (id < 0) continue;
                ev.hands++;
                // only the opening deal is purely our deck: Realm Warp (card 51) swaps hands, so later rounds can hold the bot's cards
                if (f.round === 1 && !inDeck.has(id)) ev.handsOutside++;
                if (slot < 0 && rules.isLegalPlay(id, f.energyYou, f.vaultYou) && !((f.lockMask >> i) & 1)) slot = i;
            }
            c.play(slot);
        },
        onMatchEnd: () => { ev.ends++; },
    });
    c.connect('ConsTest', '', 3);
    for (let i = 0; i < 50 && !ready; i++) await wait(100);
    return { c, ev, ready };
}

{   // 1. QUEUE before any deck was submitted
    const { c, ev, ready } = await open();
    check(ready, 'WELCOME received on a mode-3 connection');
    c.queue(1);
    await wait(500);
    check(ev.errors.includes(4) && ev.queued === 0, 'QUEUE with no deck submitted is refused (ERROR 4) and does not queue');
    c.close();
}
{   // 2. an illegal deck
    const { c, ev } = await open();
    c.draftResume(new Array(23).fill(1));
    await wait(500);
    check(ev.errors.includes(3) && ev.done.length === 0, 'an illegal deck (23 copies of one card) is refused by the server (ERROR 3)');
    c.close();
}
{   // 2b. a deck of the right size and wrong shape (twelve singles, four pairs, one triple)
    const { c, ev } = await open();
    const bad = [];
    for (let i = 0; i < 12; i++) bad.push(i);
    for (let i = 0; i < 4; i++) bad.push(20 + i, 20 + i);
    bad.push(50, 50, 50);
    c.draftResume(bad);
    await wait(500);
    check(bad.length === 23 && ev.errors.includes(3) && ev.done.length === 0, 'a 23-card deck with the wrong shape is refused too');
    c.close();
}
{   // 3. a legal deck: echoed, queued, played; then again on the same connection
    const { c, ev } = await open();
    c.draftResume(deck);
    await wait(500);
    check(ev.done.length === 1 && JSON.stringify(ev.done[0].cards) === JSON.stringify(deck), 'a legal deck is echoed back as DRAFT_DONE, card for card');
    check(ev.done.length === 1 && ev.done[0].deckId > 0, 'DRAFT_DONE carries a deck id');
    c.queue(1);
    for (let i = 0; i < 150 && ev.ends < 1; i++) await wait(100);
    check(ev.queued >= 1, 'QUEUED after the deck was accepted');
    check(ev.found.length >= 1, 'MATCH_FOUND (the constructed bot picked us up)');
    check(ev.found.length >= 1 && ev.found[0].oppKind === 1, 'the opponent is a bot');
    check(ev.ends === 1, 'the whole match was played to MATCH_END');
    check(ev.rounds > 0 && ev.hands > 0, `rounds were played (${ev.rounds} rounds, ${ev.hands} cards dealt)`);
    check(ev.handsOutside === 0, 'every card of the opening hand was in the deck we built');
    ev.ends = 0; ev.found.length = 0;
    c.queue(1);
    for (let i = 0; i < 150 && ev.ends < 1; i++) await wait(100);
    check(ev.found.length >= 1 && ev.ends === 1, 'queueing again on the same connection plays the same deck again');
    check(ev.handsOutside === 0, 'the second opening hand too');
    check(ev.errors.length === 0, `no server errors on the good connection (${JSON.stringify(ev.errors)})`);
    c.close();
}
{   // 4. a random-queue connection must not be able to submit a constructed deck (DRAFT_RESUME is mode 2 / 3 only)
    const c = new DeadweightClient(URL, { onState: () => {}, onError: () => {} });
    const errs = [];
    let ready = false;
    const c2 = new DeadweightClient(URL, { onState: (s) => { if (s === 'ready') ready = true; }, onError: (f) => errs.push(f.code), onDraftDone: () => errs.push('done') });
    c2.connect('ConsTest0', '', 0);
    for (let i = 0; i < 50 && !ready; i++) await wait(100);
    c2.draftResume(deck);
    await wait(500);
    check(ready && errs.length === 1 && errs[0] === 4, 'a random-queue (mode 0) connection cannot submit a deck (ERROR 4)');
    c2.close();
    void c;
}

console.log(fails ? `FAILED (${fails})` : 'PASSED');
process.exit(fails ? 1 : 0);
