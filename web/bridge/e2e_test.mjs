// Headless end-to-end smoke test: drives the REAL compiled browser client (dist/client.js,
// dist/proto.js -- the exact same code the browser loads) through a full match against a live
// dw_bot, over the real ws-tcp-bridge, against a real dw_server. Not a mock of anything.
import { DeadweightClient } from '../dist/client.js';
import * as rules from '../dist/generated/CardRules.js';
import { WebSocket } from 'ws';

// Node has no global WebSocket in this version; supply the 'ws' package's implementation,
// matching the browser's own global exactly enough for client.ts's usage (readyState/OPEN/send/
// onopen/onmessage/onclose/onerror, binaryType).
globalThis.WebSocket = WebSocket;

let matchEnded = false;
let roundsPlayed = 0;

const client = new DeadweightClient('ws://localhost:8765', {
    onLog: (l) => console.log('[log]', l),
    onState: (s) => console.log('[state]', s),
    onQueued: (w) => console.log('[queued] waiting=', w),
    onMatchFound: (f) => console.log('[match_found]', f),
    onRoundStart: (f) => {
        roundsPlayed++;
        console.log(`[round_start] round=${f.round} hull=${f.hullYou}/${f.hullOpp} energy=${f.energyYou}`);
        // Real use of the PARENA-compiled rules: pick the first legal card, else pass.
        let slot = -1;
        for (let i = 0; i < 4; i++) {
            const id = f.hand[i];
            if (id >= 0 && rules.isLegalPlay(id, f.energyYou, f.vaultYou) && !((f.lockMask >> i) & 1)) {
                slot = i;
                break;
            }
        }
        client.play(slot);
    },
    onRoundResult: (f) => console.log(`[round_result] round=${f.round} you=${f.cardYou} opp=${f.cardOpp} dmgToOpp=${f.dmgToOpp} dmgToYou=${f.dmgToYou}`),
    onMatchEnd: (f) => {
        console.log('[match_end]', f, 'rounds played:', roundsPlayed);
        matchEnded = true;
    },
    onError: (f) => console.error('[error]', f),
});

client.connect('E2ENodeBot');

setTimeout(() => {
    client.queue();
}, 300);

setTimeout(() => {
    if (!matchEnded) {
        console.error('FAIL: match did not end within timeout');
        process.exit(1);
    }
    if (roundsPlayed === 0) {
        console.error('FAIL: zero rounds played');
        process.exit(1);
    }
    console.log(`PASS: real end-to-end match completed through the compiled browser client over WebSocket, ${roundsPlayed} rounds played`);
    process.exit(0);
}, 8000);
