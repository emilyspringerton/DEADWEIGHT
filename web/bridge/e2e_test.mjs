// Headless end-to-end smoke test: drives the REAL compiled browser client (dist/client.js,
// dist/proto.js -- the exact same code the browser loads) through a full match against a live
// dw_bot, over the real ws-tcp-bridge, against a real dw_server. Not a mock of anything.
import { DeadweightClient } from '../dist/client.js';
import * as rules from '../dist/generated/CardRules.js';
import * as fx from '../dist/fx.js';
import { WebSocket } from 'ws';

// Node has no global WebSocket in this version; supply the 'ws' package's implementation,
// matching the browser's own global exactly enough for client.ts's usage (readyState/OPEN/send/
// onopen/onmessage/onclose/onerror, binaryType).
globalThis.WebSocket = WebSocket;

let matchEnded = false;
let roundsPlayed = 0;
let fxComputed = 0;
let fxFailed = 0;
let beforeArmorYou = 0, beforeArmorOpp = 0, beforeVaultYou = 0, beforeVaultOpp = 0;

const client = new DeadweightClient('ws://localhost:8765', {
    onLog: (l) => console.log('[log]', l),
    onState: (s) => console.log('[state]', s),
    onQueued: (w) => console.log('[queued] waiting=', w),
    onMatchFound: (f) => console.log('[match_found]', f),
    onRoundStart: (f) => {
        roundsPlayed++;
        beforeArmorYou = f.armorYou; beforeArmorOpp = f.armorOpp;
        beforeVaultYou = f.vaultYou; beforeVaultOpp = f.vaultOpp;
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
    onRoundResult: (f) => {
        console.log(`[round_result] round=${f.round} you=${f.cardYou} opp=${f.cardOpp} dmgToOpp=${f.dmgToOpp} dmgToYou=${f.dmgToYou}`);
        // Real use of fx.ts's own computeTimeline -- the exact same shared PARENA decision layer
        // apps/gui/fx.c now calls too (core/fx_rules.c, from the same fx_rules.prn). This is a
        // pure-logic check (no DOM/Canvas/WebAudio available in Node) that the browser's own
        // input-mapping glue produces sane output against a REAL live match, not synthetic vectors.
        try {
            const input = {
                cardYou: f.cardYou, cardOpp: f.cardOpp, effYou: f.effYou, effOpp: f.effOpp,
                cancelledYou: !!(f.flagsYou & 1), cancelledOpp: !!(f.flagsOpp & 1),
                dmgYou: f.dmgToYou, dmgOpp: f.dmgToOpp, healYou: f.healYou, healOpp: f.healOpp,
                armorBeforeYou: beforeArmorYou, armorAfterYou: f.armorYou,
                armorBeforeOpp: beforeArmorOpp, armorAfterOpp: f.armorOpp,
                vaultBeforeYou: beforeVaultYou, vaultAfterYou: f.vaultYou,
                vaultBeforeOpp: beforeVaultOpp, vaultAfterOpp: f.vaultOpp,
                energyDeltaYou: 0, energyDeltaOpp: 0,
                newStatusYou: false, newStatusOpp: false,
                disabledYou: !!(f.flagsOpp & 8), disabledOpp: !!(f.flagsYou & 8),
                swapped: !!((f.flagsYou & 16) || (f.flagsOpp & 16)),
            };
            const t = fx.computeTimeline(input);
            fxComputed++;
            if (t.scenario < 0 || t.scenario > 10 || t.totalMs <= 0 || t.totalMs > 9000 || !Number.isFinite(t.totalMs)) {
                fxFailed++;
                console.error(`FX SANITY FAIL round ${f.round}:`, t);
            } else {
                console.log(`[fx] scenario=${fx.scenarioName(t)} win=${t.win} crit=${t.crit} totalMs=${Math.round(t.totalMs)}`);
            }
        } catch (e) {
            fxFailed++;
            console.error(`FX EXCEPTION round ${f.round}:`, e);
        }
    },
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
    if (fxFailed > 0) {
        console.error(`FAIL: ${fxFailed}/${fxComputed} fx.computeTimeline() calls failed sanity checks`);
        process.exit(1);
    }
    if (fxComputed === 0) {
        console.error('FAIL: fx.computeTimeline() was never called');
        process.exit(1);
    }
    console.log(`PASS: real end-to-end match completed through the compiled browser client over WebSocket, ${roundsPlayed} rounds played, ${fxComputed} fx timelines computed (0 failures)`);
    process.exit(0);
}, 8000);
