// DEADWEIGHT browser client — UI glue. Hand-written host (this file + index.html), PARENA is the
// decision layer (generated/CardRules.ts — isLegalPlay, cardKind, cardKeyword, etc. are called
// directly, never re-implemented here), same "PARENA is the trigger, host does the work" idiom
// every other DEADWEIGHT client already follows (Android Java shell, Windows SDL2 GUI).

import { DeadweightClient } from './client.js';
import type { ClientEvents } from './client.js';
import { initWasmProto } from './wasmProto.js';
import type { RoundStart } from './wasmProto.js';
import * as rules from './generated/CardRules.js';
import * as fxWasm from './fxWasm.js';
import * as account from './account.js';
import * as social from './social.js';
import * as sso from './sso.js';
import { DeckBuilder } from './constructed.js';

type CardEntry = { id: number; name: string; kind: string; keyword: string; cost: number; power: number; credit: number; text: string };
type CardsData = { version: string; cards: CardEntry[] };

// Exact match to apps/gui/main.c's KIND_NAME/KIND_COL (Windows/Linux desktop client) --
// docs/BRAND_STYLE_GUIDE.md Section 2A is the single source of truth for both clients.
const KIND_NAMES = ['OFFENSE', 'OPERATIONS', 'DEFENSE'];
const KIND_COLORS = ['#D7463C', '#E1A028', '#468CE6'];

const $ = (id: string) => document.getElementById(id)!;

let client: DeadweightClient;
let clientHandlers: ClientEvents;
let connBridge = '', connName = '', connToken = '';
// Draft mode (apps/gui/main.c's A.mode == DW_MODE_DRAFT flow): HELLO mode 2 -> QUEUE -> DRAFT_OFFER x16 ->
// DRAFT_DONE -> QUEUED -> match; between matches the Draft Hub (wins / burned proxies / resume / abort).
let draftActive = false;
let draftWantQueue = false;
let draftAwaitingResumeQueue = false;
let draftResumeCards: number[] | null = null;
let draftDeck: number[] = [];
let draftDeckId = 0;
let draftPicks: { card: number; mult: number }[] = [];
let draftPend = { card: -1, mult: 0 };
let draftOfferCards: number[] = [-1, -1];
let draftLeft: number[] = [0, 0, 0];
// Constructed mode (HELLO mode 3): the player builds a deck from the whole card pool (constructed.ts / deck.ts), this client connects
// in mode 3, submits it with DRAFT_RESUME, and queues. connMode is the mode of the CURRENT connection: every mode switch reconnects.
let connMode: 0 | 2 | 3 = 0;
let consBuilder: DeckBuilder | null = null;
let consWantCards: number[] | null = null;   // a deck waiting for the constructed connection to become ready
let consDeckInPlay: number[] = [];
let currentAccount: account.Account | null = null;
let idunaBaseUrl = '';
// Fetched at startup (not a static JSON import) so this works unbundled, straight from
// index.html's <script type="module">, with no import-assertion browser-compatibility gap.
let cardsData: CardsData = { version: '', cards: [] };
let currentHand: number[] = [];
let currentEnergy = 0;
let currentVault = 0;
let lockMask = 0;
let locked = false;
let oppName = '';
let matchSeed = 0;
// "before" snapshot for the round about to resolve, captured at ROUND_START, consumed by the
// ROUND_RESULT that follows it -- see fx_wasm.c's own wasm_fx_energy_estimate header comment for
// why energy/status use a deferred, one-round-lagged scheme instead (docs/WIRE_PROTOCOL.md's
// ROUND_RESULT carries no energy-delta or post-round-status field at all -- the real, structural
// reason -- so this mirrors apps/gui/main.c's own real fx_finish_round/pend_valid/rs_energy
// pattern exactly: a round's animation only actually starts once the NEXT ROUND_START confirms
// what its energy/status outcome was, or MATCH_END confirms there is no next round).
let beforeHullYou = 0, beforeHullOpp = 0, beforeArmorYou = 0, beforeArmorOpp = 0, beforeVaultYou = 0, beforeVaultOpp = 0;
let rsEnergyYou = 0, rsEnergyOpp = 0;
let lastStatusYou = 0, lastStatusOpp = 0;
let pendRound: fxWasm.FxRoundInput | null = null;
let fxTickHandle = 0;
let fxLastTs = 0;
// Mirrors apps/gui/main.c's own persistent A.rv_you/A.rv_opp/A.have_reveal/A.rv_round/A.rv_dy/
// A.rv_do -- fed to fxWasm.tick() every frame so fx_draw_arena can draw its own "LAST ROUND" idle
// reveal (or "PICK A CARD OR PASS" before the first one) exactly like the Windows client, instead
// of leaving the canvas blank between rounds (part of the founder's real-time bug report,
// 2026-09-28: "it doesnt show the card text on the cards when you reveal it").
let haveReveal = false, rvYou = -1, rvOpp = -1, rvRound = 0, rvDmgYou = 0, rvDmgOpp = 0;
// Round countdown, mirrors apps/gui/main.c's A.deadline_at (SDL_GetTicks() + deadline_ms) exactly,
// just on the wall clock instead of SDL's tick counter -- 0 = no deadline (fast-forward servers
// send deadlineMs 0). Founder real-time, 2026-09-29: "we are missing the round timer add that to
// the display".
let roundDeadlineAt = 0;

function updateRoundTimer() {
    const el = document.getElementById('round-timer');
    if (!el) return;   // an older cached index.html (no #round-label) loses the span when the round text is set: no timer, but no error per frame
    if (!roundDeadlineAt) { el.textContent = ''; return; }
    const leftMs = Math.max(0, roundDeadlineAt - Date.now());
    el.textContent = `${Math.ceil(leftMs / 1000)}s`;
    el.classList.toggle('low', leftMs < 5000);
}

// A live UTC clock, independent of match state -- founder real-time, 2026-09-29: "add ... the UTC
// time". Ticks once a second; started immediately below, not gated on connecting.
function updateUtcClock() {
    $('utc-clock').textContent = new Date().toISOString().slice(11, 19) + ' UTC';
}
updateUtcClock();
setInterval(updateUtcClock, 1000);

function flushPendRound(energyNextYou: number | null, energyNextOpp: number | null, statusAfterYou: number, statusAfterOpp: number, lockAfter: number, lethal: boolean) {
    if (!pendRound) return;
    pendRound.energyNextYou = energyNextYou;
    pendRound.energyNextOpp = energyNextOpp;
    pendRound.statusAfterYou = statusAfterYou;
    pendRound.statusAfterOpp = statusAfterOpp;
    pendRound.lockAfter = lockAfter;
    pendRound.lethal = lethal;
    fxWasm.beginRound(pendRound);
    pendRound = null;
}

function fxTickLoop(ts: number) {
    fxTickHandle = requestAnimationFrame(fxTickLoop);
    const dt = fxLastTs ? ts - fxLastTs : 16;
    fxLastTs = ts;
    fxWasm.tick(dt, $('fx-canvas') as HTMLCanvasElement, { you: rvYou, opp: rvOpp, have: haveReveal, round: rvRound, dmgYou: rvDmgYou, dmgOpp: rvDmgOpp });
    updateRoundTimer();
}
// Duel Phase 2 (S537): set by playDuel() right before a duel's PLAY button, consumed (and
// cleared) the next time the client reaches 'ready' -- either immediately, if it's already
// connected, or once WELCOME arrives if the duel was clicked before connect() finished.
let pendingMatchToken: string | null = null;

function log(line: string) {
    const el = $('log');
    const row = document.createElement('div');
    row.textContent = line;
    el.appendChild(row);
    el.scrollTop = el.scrollHeight;
}

// core/match.h's DW_ST_BURN=1/DW_ST_REGEN=2/DW_ST_HIDDEN=4 -- surfaced here so a persistent status
// tick (which silently adds to next round's "took"/"dealt" with no card of its own) is actually
// visible in the log instead of looking like unexplained damage. Found live, 2026-09-29: a player
// died to a Defense-mirror round that dealt 0 combat damage -- the real killer was a burn DOT from
// two rounds earlier that never showed anywhere on screen or in this log.
function statusLabel(bits: number): string {
    const parts: string[] = [];
    if (bits & 1) parts.push('BURNING');
    if (bits & 2) parts.push('REGEN');
    if (bits & 4) parts.push('HIDDEN');
    return parts.length ? ` [${parts.join('+')}]` : '';
}

function cardLabel(id: number): string {
    if (id === -1) return 'PASS';
    const c = cardsData.cards[id];
    if (!c) return `#${id}`;
    return `${c.name} (${KIND_NAMES[rules.cardKind(id)]}${c.keyword ? '/' + c.keyword : ''}, cost ${c.cost}${c.credit ? '+' + c.credit + 'cr' : ''}, pow ${c.power})`;
}

// Real interaction-model parity, not just visual: apps/gui/main.c's click() handler shows clicking
// a hand card (or the PASS button) only calls select_slot() -- it doesn't submit anything. Only
// LOCK IN calls lock_selected(), which is what actually sends DW_C_PLAY. selectedSlot mirrors
// A.sel (null = A.sel's -2 "nothing chosen" sentinel, -1 = PASS chosen, 0..3 = a hand slot).
let selectedSlot: number | null = null;

function slotLegal(slot: number): boolean {
    const id = currentHand[slot];
    return id >= 0 && rules.isLegalPlay(id, currentEnergy, currentVault) && !((lockMask >> slot) & 1);
}

function selectSlot(slot: number) {
    if (locked) return;
    if (slot !== -1 && !slotLegal(slot)) return;
    selectedSlot = slot;
    renderHand();
    updateActionButtons();
}

function updateActionButtons() {
    const passBtn = $('pass-btn') as HTMLButtonElement;
    const lockBtn = $('lockin-btn') as HTMLButtonElement;
    passBtn.disabled = locked;
    passBtn.textContent = selectedSlot === -1 ? (locked ? 'Passed' : 'Pass *') : 'Pass';
    const canLock = !locked && selectedSlot !== null;
    lockBtn.disabled = !canLock;
    lockBtn.textContent = locked ? 'Locked' : 'Lock in';
    lockBtn.classList.toggle('can-lock', canLock);
}

function lockIn() {
    if (locked || selectedSlot === null) return;
    locked = true;
    renderHand();
    updateActionButtons();
    client.play(selectedSlot);
    log(`locked slot ${selectedSlot}: ${cardLabel(selectedSlot === -1 ? -1 : currentHand[selectedSlot])}`);
}

// card_box()'s real layout: a solid-panel card with a full-width kind-colored header bar holding
// the name, then COST/PWR, kind/keyword, wrapped rules text, and a small slot number bottom-left
// -- not a plain button with a colored top border (the old approximation).
function renderHand() {
    const handEl = $('hand');
    handEl.innerHTML = '';
    currentHand.forEach((id, slot) => {
        const btn = document.createElement('button');
        btn.className = 'card-btn';
        if (id === -1) {
            btn.innerHTML = '<div class="card-header" style="background:var(--lock);">—</div><div class="card-body">(empty slot)</div>';
            btn.disabled = true;
        } else {
            const c = cardsData.cards[id];
            const kind = rules.cardKind(id);
            const legal = slotLegal(slot);
            btn.innerHTML =
                `<div class="card-header" style="background:${KIND_COLORS[kind]};">${c ? c.name : '#' + id}</div>` +
                `<div class="card-body">` +
                `<div class="card-meta">Cost ${c ? c.cost : '?'}${c && c.credit ? ' +' + c.credit + 'cr' : ''} · Pwr ${c ? c.power : '?'}</div>` +
                `<div class="card-kind">${KIND_NAMES[kind]}${c && c.keyword ? ' / ' + c.keyword : ''}</div>` +
                (c && c.text ? `<span class="rules-text">${c.text}</span>` : '') +
                `</div><div class="card-slot-num">(${slot + 1})</div>`;
            btn.disabled = !legal || locked;
            btn.classList.toggle('selected', !locked && selectedSlot === slot);
            btn.classList.toggle('locked-in', locked && selectedSlot === slot);
            btn.onclick = (ev) => { selectSlot(slot); if (ev.detail >= 2 && selectedSlot === slot) lockIn(); };
        }
        handEl.appendChild(btn);
    });
}

// hull_bar()/pips(): the opponent's own bars use ROUND_START's *_opp fields, honoring Merkle
// Blindness's real hidden sentinels (docs/WIRE_PROTOCOL.md: energy_opp/armor_opp == 255 and
// vault_opp == -128 while the opponent is hidden) rather than always showing a number.
function sideText(armor: number, vault: number): string {
    if (armor === 255) return 'A? $?';
    return `A${armor} $${vault}`;
}

// Founder real-time, 2026-09-29: after a "dealt 0" round turned out to be correct (the opponent's
// banked armor fully absorbed a winning triangle matchup -- see docs/CARD_MODE_RULES.md), the text
// log itself had no way to show that without cross-referencing the HUD side-text. Same hidden
// sentinels as sideText()/pipsHtml() (docs/WIRE_PROTOCOL.md: energy_opp/armor_opp == 255,
// vault_opp == -128 while Merkle Blindness is active).
function meterLabel(energy: number, armor: number, vault: number): string {
    const e = energy === 255 ? '?' : `${energy}`;
    const a = armor === 255 ? '?' : `${armor}`;
    const v = vault === -128 ? '?' : `${vault}`;
    return `energy ${e}, armor ${a}, credits ${v}`;
}

function pipsHtml(energy: number, hidden: boolean): string {
    if (hidden) return '<span style="opacity:0.7;">? (hidden)</span>';
    let html = '';
    for (let i = 0; i < 6; i++) html += `<span class="epip${i < energy ? ' full' : ''}"></span>`;
    return html;
}

function renderBars(f: RoundStart) {
    // the label has its own span: setting #round-num-big's textContent would delete the #round-timer span beside it
    ($('round-label') || $('round-num-big')).textContent = `Round ${f.round}/${rules.maxRounds()}`;

    const startHull = rules.startHull();
    const oppFrac = Math.max(0, f.hullOpp) / startHull;
    ($('hbar-opp-fill') as HTMLElement).style.width = `${Math.min(1, oppFrac) * 100}%`;
    ($('hbar-opp-fill') as HTMLElement).classList.toggle('low', f.hullOpp * 3 <= startHull);
    ($('hbar-opp-text') as HTMLElement).textContent = `${oppName || 'OPP'} ${Math.max(0, f.hullOpp)}/${startHull}`;
    ($('hbar-opp-side') as HTMLElement).textContent = sideText(f.armorOpp, f.vaultOpp);
    ($('epips-opp') as HTMLElement).innerHTML = pipsHtml(f.energyOpp, f.energyOpp === 255);
    ($('hand-count-opp') as HTMLElement).textContent = `Hand ${f.oppHandSize}`;

    const youFrac = Math.max(0, f.hullYou) / startHull;
    ($('hbar-you-fill') as HTMLElement).style.width = `${Math.min(1, youFrac) * 100}%`;
    ($('hbar-you-fill') as HTMLElement).classList.toggle('low', f.hullYou * 3 <= startHull);
    ($('hbar-you-text') as HTMLElement).textContent = `YOU ${Math.max(0, f.hullYou)}/${startHull}`;
    ($('hbar-you-side') as HTMLElement).textContent = sideText(f.armorYou, f.vaultYou);
    ($('epips-you') as HTMLElement).innerHTML = pipsHtml(f.energyYou, false);
}

// Founder real-time bug report, 2026-09-28: "i can get killt and it doesnt show my health go to
// the bottom." Root cause: the hull/armor/vault bars above only ever refresh from renderBars(),
// called on ROUND_START -- but the round that ends a match is never followed by another
// ROUND_START, so a lethal hit's own hull change never reached the DOM at all. apps/gui/main.c's
// own DW_S_MATCH_END handler doesn't have this gap: it calls fx_targets(0) to push the meters to
// the final round's real post-round values immediately, before that round's own animation plays
// (main.c:541-546). This is the same push, for the DOM bars this client draws instead of SDL2 ones.
function applyFinalMeters(hullYou: number, hullOpp: number, armorYou: number, armorOpp: number, vaultYou: number, vaultOpp: number) {
    const startHull = rules.startHull();
    const oppFrac = Math.max(0, hullOpp) / startHull;
    ($('hbar-opp-fill') as HTMLElement).style.width = `${Math.min(1, oppFrac) * 100}%`;
    ($('hbar-opp-fill') as HTMLElement).classList.toggle('low', hullOpp * 3 <= startHull);
    ($('hbar-opp-text') as HTMLElement).textContent = `${oppName || 'OPP'} ${Math.max(0, hullOpp)}/${startHull}`;
    ($('hbar-opp-side') as HTMLElement).textContent = sideText(armorOpp, vaultOpp);
    const youFrac = Math.max(0, hullYou) / startHull;
    ($('hbar-you-fill') as HTMLElement).style.width = `${Math.min(1, youFrac) * 100}%`;
    ($('hbar-you-fill') as HTMLElement).classList.toggle('low', hullYou * 3 <= startHull);
    ($('hbar-you-text') as HTMLElement).textContent = `YOU ${Math.max(0, hullYou)}/${startHull}`;
    ($('hbar-you-side') as HTMLElement).textContent = sideText(armorYou, vaultYou);
}

function setStatus(s: string) {
    $('status').textContent = s;
}

// Rings = this player's total DEADWEIGHT match wins -- IDUNA's existing game_player_stats.wins
// (social.getProfile, already public/no-token, the same call the Friends panel below already
// makes), not a new counter. Founder real-time, 2026-09-29: "for now you cant do anything with
// them but have it show the rings in the interface next to the utc clock". Best-effort: a failed
// read leaves the last-known count on screen rather than erroring out over a cosmetic badge.
async function refreshRings() {
    if (!currentAccount) return;
    try {
        const p = await social.getProfile(idunaBaseUrl, currentAccount.playerID);
        ($('rings-count') as HTMLElement).textContent = `${p.wins}`;
    } catch { /* cosmetic only -- see comment above */ }
}

// enterGame is start()'s own real tail, extracted (2026-09-25) so the SSO path can
// reach the exact same "connected and playing" state without duplicating the client wiring --
// both paths only differ in HOW currentAccount got resolved (bootstrap vs. a fresh
// register+claim), never in what happens once it's resolved.
let queueAfterReconnect = false;

async function enterGame(idunaUrl: string, bridgeUrl: string, fallbackName: string) {
    // Must resolve before client.connect() below ever calls into the wasm codec -- initWasmProto
    // is idempotent (see its own header comment), so this is a safe no-op on the path that already
    // awaited it (start()) and the real fix for the two paths that used not to (signInWithIduna,
    // createAccount).
    await initWasmProto();
    idunaBaseUrl = idunaUrl;
    $('setup').style.display = 'none';
    $('game').style.display = 'block';
    // Real bug, found live (2026-09-28, founder repro: connect then immediately click Queue):
    // the button used to start enabled and only got disabled *after* being clicked, so a click
    // that landed before the server's WELCOME (IDUNA token verification is a real network round
    // trip, not instant) sent QUEUE while the connection was still S_NEEDAUTH/S_VERIFYING on the
    // server side -- an automatic DW_ERR_BAD_STATE (code 4), which also closes the connection
    // (send_error always sets close_after_flush). Disabled here, at the very start of connecting,
    // and re-enabled only by the onState('ready') handler below -- the same instant WELCOME
    // actually arrives.
    ($('queue-btn') as HTMLButtonElement).disabled = true;

    if (currentAccount) { initSocial(); refreshRings(); }
    updateSaveBtn();

    cardsData = await (await fetch('./src/generated/cards.json')).json();
    log(`loaded ${cardsData.cards.length}-card catalog (v${cardsData.version})`);
    if (!consBuilder) {
        consBuilder = new DeckBuilder({
            cards: cardsData.cards,
            kindOf: (id) => rules.cardKind(id),
            cardBox: draftCardBox,
            kindColor: (k) => KIND_COLORS[k],
            kindName: (k) => KIND_NAMES[k],
            onQueue: (cards) => startConstructed(cards),
            onBack: () => showScreen('game'),
        });
    }

    // The real round-resolution animation/audio engine (apps/gui/fx.c + apps/gui/sfx.c, compiled
    // unmodified to wasm -- see fxWasm.ts's own header comment for scope/honest limits). Started
    // here (inside enterGame's own click-driven call chain) so resetAudioClock()'s AudioContext
    // creation happens on a real user gesture, satisfying browser autoplay policy.
    await fxWasm.initFxWasm($('fx-canvas') as HTMLCanvasElement);
    fxWasm.resetAudioClock();
    fxLastTs = 0;
    if (!fxTickHandle) fxTickHandle = requestAnimationFrame(fxTickLoop);

    connBridge = bridgeUrl;
    connName = currentAccount ? currentAccount.displayName : fallbackName;
    connToken = currentAccount ? currentAccount.token : '';
    clientHandlers = {
        onState(s) {
            setStatus(s);
            const queueBtn = $('queue-btn') as HTMLButtonElement;
            const consBtn = document.getElementById('cons-btn') as HTMLButtonElement | null;
            if (consBtn) consBtn.disabled = draftActive || !(s === 'ready' || s === 'closed' || s === 'error');
            if (s === 'ready' && connMode === 3 && consWantCards) {
                // the constructed connection is up: submit the built deck (DRAFT_DONE echoes it back and draftDone() queues)
                const deck = consWantCards;
                consWantCards = null;
                client.draftResume(deck);
                queueBtn.disabled = true;
                if (consBtn) consBtn.disabled = true;
            } else if (s === 'ready' && draftResumeCards) {
                // RESUME UPLINK after a reconnect: resubmit the persisted deck; DRAFT_DONE echoes it back
                // and draftDone() then queues same_deck (dw_server does not auto-queue this path).
                client.draftResume(draftResumeCards);
                draftAwaitingResumeQueue = true;
                draftResumeCards = null;
                queueBtn.disabled = true;
            } else if (s === 'ready' && draftWantQueue) {
                draftWantQueue = false;
                client.queue(0);   // draft mode: no same_deck byte = draft a new deck first
                queueBtn.disabled = true;
            } else if (s === 'ready' && pendingMatchToken) {
                const tok = pendingMatchToken;
                pendingMatchToken = null;
                client.queue(0, tok);
                queueBtn.disabled = true;
            } else {
                // Only 'ready' means the server has actually WELCOMEd this connection -- every
                // other state (connecting/queued/in_match/error/closed) must keep this disabled,
                // see enterGame's own header comment on the ERROR-4 race this closes.
                // closed/error stay clickable: the click reconnects and queues (see queue-btn handler).
                queueBtn.disabled = !(s === 'ready' || s === 'closed' || s === 'error');
                if (s === 'ready' && queueAfterReconnect) { queueAfterReconnect = false; client.queue(); queueBtn.disabled = true; }
            }
        },
        onLog(line) {
            log(line);
        },
        onQueued(waiting) {
            setStatus(`queued (${waiting} waiting)`);
        },
        onMatchFound(f) {
            oppName = f.oppName;
            matchSeed = f.seed;
            fxWasm.resetMatch();
            pendRound = null;
            haveReveal = false; rvYou = -1; rvOpp = -1; rvRound = 0; rvDmgYou = 0; rvDmgOpp = 0;
            roundDeadlineAt = 0;
            const banner = $('end-banner') as HTMLElement;
            banner.classList.remove('show'); banner.textContent = '';
            log(`MATCH_FOUND vs ${f.oppName} (${f.oppKind === 1 ? 'bot' : 'human'}), seat ${f.seat}, seed ${f.seed}`);
            $('cons-edit').style.display = 'none';
            ($('opp-name') as HTMLElement).textContent = `${f.oppName} (${f.oppKind === 1 ? 'bot' : 'human'})`;
            $('match').style.display = 'block';
        },
        onRoundStart(f) {
            // This ROUND_START is the "next round start" apps/gui/main.c's own fx_finish_round
            // waits for -- it's what actually STARTS the previous round's animation, now that its
            // real energy delta and post-round status are knowable (see this file's own header
            // comment above and fx_wasm.c's wasm_fx_energy_estimate comment for the full why).
            flushPendRound(f.energyYou, f.energyOpp, f.statusYou, f.statusOpp, f.lockMask, false);
            fxWasm.setRedline(f.hullYou, f.hullOpp);
            fxWasm.statusChanged(f.statusYou, f.statusOpp, f.lockMask, true);
            fxWasm.setMeters(f.hullYou, f.hullOpp, f.armorYou, f.armorOpp === 255 ? 0 : f.armorOpp,
                f.energyYou, f.energyOpp === 255 ? 0 : f.energyOpp, f.vaultYou, f.vaultOpp === -128 ? 0 : f.vaultOpp, false);

            currentHand = f.hand;
            currentEnergy = f.energyYou;
            currentVault = f.vaultYou;
            lockMask = f.lockMask;
            locked = false;
            selectedSlot = null;
            roundDeadlineAt = f.deadlineMs ? Date.now() + f.deadlineMs : 0;
            beforeHullYou = f.hullYou; beforeHullOpp = f.hullOpp;
            beforeArmorYou = f.armorYou; beforeArmorOpp = f.armorOpp;
            beforeVaultYou = f.vaultYou; beforeVaultOpp = f.vaultOpp;
            rsEnergyYou = f.energyYou; rsEnergyOpp = f.energyOpp;
            lastStatusYou = f.statusYou; lastStatusOpp = f.statusOpp;
            renderBars(f);
            renderHand();
            updateActionButtons();
            log(`round ${f.round} start: hull ${f.hullYou}/${f.hullOpp} | you: ${meterLabel(f.energyYou, f.armorYou, f.vaultYou)}${statusLabel(f.statusYou)} | opp: ${meterLabel(f.energyOpp, f.armorOpp, f.vaultOpp)}${statusLabel(f.statusOpp)}`);
        },
        onPlayReject(f) {
            // Mirrors apps/gui/main.c's own on-reject handling exactly: DW_REJ_ALREADY_LOCKED (4)
            // leaves A.locked/A.sel alone (nothing to reset, this client shouldn't have been able
            // to double-submit anyway); any other reason clears both so the player can pick again.
            if (f.reason !== 4) { locked = false; selectedSlot = null; }
            renderHand();
            updateActionButtons();
            log(`play rejected (reason ${f.reason}) — try again`);
        },
        onRoundResult(f) {
            // lastStatusYou/Opp is the status this round STARTED with (set in onRoundStart) -- if
            // you were already burning/regenerating going in, some of this round's dealt/took total
            // is that tick, not the card clash. Flagged here rather than silently folded into the
            // number so a death like the one above is traceable from the log alone.
            const tickNote = (lastStatusYou & 1) || (lastStatusOpp & 1) ? ` (burn ticking${lastStatusYou & 1 ? ': you' : ''}${lastStatusYou & 1 && lastStatusOpp & 1 ? ' + ' : ''}${lastStatusOpp & 1 ? 'opp' : ''})` : '';
            log(`round ${f.round} result: you played ${cardLabel(f.cardYou)}, opp played ${cardLabel(f.cardOpp)} — dealt ${f.dmgToOpp}, took ${f.dmgToYou}, hull now ${f.hullYou}/${f.hullOpp}${tickNote}`);
            // Mirrors apps/gui/main.c's own DW_S_ROUND_RESULT handling exactly: A.rv_you/A.rv_opp/
            // A.rv_dy/A.rv_do/A.rv_round/A.have_reveal update immediately here, not deferred with
            // the rest of pendRound -- fx_draw_arena's idle "LAST ROUND" panel needs to show this
            // round's real result right away, even before the animation that plays it starts.
            haveReveal = true;
            rvYou = f.effYou >= 0 ? f.effYou : f.cardYou;
            rvOpp = f.effOpp >= 0 ? f.effOpp : f.cardOpp;
            rvDmgYou = f.dmgToYou; rvDmgOpp = f.dmgToOpp; rvRound = f.round;
            // Defensive flush matching apps/gui/main.c's own safety net (DW_S_ROUND_RESULT: "if
            // (A.pend_valid) fx_finish_round(0, NULL)") -- the wire protocol's own state machine
            // never actually lets two ROUND_RESULTs arrive without a ROUND_START between them, so
            // this should be a no-op in practice, not the normal path.
            if (pendRound) flushPendRound(null, null, pendRound.statusBeforeYou, pendRound.statusBeforeOpp, 0, false);
            pendRound = {
                round: f.round,
                cardYou: f.cardYou, cardOpp: f.cardOpp, effYou: f.effYou, effOpp: f.effOpp,
                dmgYou: f.dmgToYou, dmgOpp: f.dmgToOpp, healYou: f.healYou, healOpp: f.healOpp,
                hullBeforeYou: beforeHullYou, hullAfterYou: f.hullYou,
                hullBeforeOpp: beforeHullOpp, hullAfterOpp: f.hullOpp,
                armorBeforeYou: beforeArmorYou, armorAfterYou: f.armorYou,
                armorBeforeOpp: beforeArmorOpp, armorAfterOpp: f.armorOpp,
                vaultBeforeYou: beforeVaultYou, vaultAfterYou: f.vaultYou,
                vaultBeforeOpp: beforeVaultOpp, vaultAfterOpp: f.vaultOpp,
                rsEnergyYou, rsEnergyOpp,
                energyNextYou: null, energyNextOpp: null, // filled in by flushPendRound once known
                flagsYou: f.flagsYou, flagsOpp: f.flagsOpp,
                // statusBefore is this round's *incoming* status -- the status_you/status_opp its
                // own ROUND_START carried (captured into lastStatusYou/Opp there).
                statusBeforeYou: lastStatusYou, statusBeforeOpp: lastStatusOpp,
                statusAfterYou: 0, statusAfterOpp: 0, // filled in by flushPendRound once known
                lockAfter: 0, lethal: false, seed: (matchSeed ^ (f.round * 2654435761)) >>> 0,
            };
        },
        onMatchEnd(f) {
            const outcome = f.result === 1 ? 'WIN' : f.result === 0 ? 'LOSS' : 'DRAW';
            roundDeadlineAt = 0;
            fxWasm.matchEnd(f.result);
            log(`MATCH_END: ${outcome} (reason ${f.reason})`);
            // Big, prominent VICTORY/DEFEAT/DRAW text -- apps/gui/main.c's own S_END screen
            // (text_c(W/2, 220, 6, ...)) already does this on desktop; the browser client only
            // ever had the small #status line, which the founder found wasn't showing it either
            // way. Synced with fx_match_end's own particle burst + sfx_match_end audio above.
            const banner = $('end-banner') as HTMLElement;
            banner.textContent = f.result === 1 ? 'VICTORY' : f.result === 0 ? 'DEFEAT' : 'DRAW';
            banner.style.color = f.result === 1 ? 'var(--good)' : f.result === 0 ? 'var(--bad)' : 'var(--dim)';
            banner.classList.add('show');
            if (f.result === 1) refreshRings();
            if (pendRound) {
                // The literal "I got killed and it didn't show my health" bug -- see
                // applyFinalMeters's own header comment for the full why.
                applyFinalMeters(pendRound.hullAfterYou, pendRound.hullAfterOpp, pendRound.armorAfterYou, pendRound.armorAfterOpp,
                    pendRound.vaultAfterYou, pendRound.vaultAfterOpp);
                fxWasm.setMeters(pendRound.hullAfterYou, pendRound.hullAfterOpp,
                    pendRound.armorAfterYou, pendRound.armorAfterOpp === 255 ? 0 : pendRound.armorAfterOpp,
                    rsEnergyYou, rsEnergyOpp === 255 ? 0 : rsEnergyOpp,
                    pendRound.vaultAfterYou, pendRound.vaultAfterOpp === -128 ? 0 : pendRound.vaultAfterOpp, false);
                fxWasm.setRedline(pendRound.hullAfterYou, pendRound.hullAfterOpp);
            }
            // No further ROUND_START is coming -- finish the last round's animation now, with an
            // unknown energy delta and an unchanged status, matching apps/gui/main.c's own
            // DW_S_MATCH_END handling exactly (A.pend.lethal = 1; fx_finish_round(0, NULL);).
            if (pendRound) flushPendRound(null, null, pendRound.statusBeforeYou, pendRound.statusBeforeOpp, 0, true);
            setStatus(`match over: ${outcome}`);
            // The Friends & Duels W/L line was only fetched once at connect, so finished matches never showed up
            // until a reload. dw_server reports the result to IDUNA right at match end; give it a moment, then refetch.
            setTimeout(() => { void initSocial(); }, 1500);
            if (draftActive && draftDeck.length) {
                $('requeue').style.display = 'none';
                openHub(true);   // S510: "players do not queue immediately after drafting" -- route through the Hub
            } else {
                $('requeue').style.display = 'inline-block';
                // constructed: the same deck can queue again at once, or go back to the builder first
                $('cons-edit').style.display = connMode === 3 ? 'inline-block' : 'none';
            }
        },
        onError(f) {
            log(`ERROR code ${f.code}`);
            // the server checks every submitted deck itself (dw_draft_deck_valid): 3 = it refused the deck, 4 = no deck to queue with
            if (connMode === 3) setStatus(f.code === 3 ? 'the server refused that deck -- build it again' : `server error ${f.code} (constructed)`);
        },
        onDraftOffer(f) { draftOffer(f); },
        onDraftDone(f) { draftDone(f); },
    };
    client = new DeadweightClient(bridgeUrl, clientHandlers);
    connMode = 0;
    client.connect(connName, connToken, 0);
    refreshTickets();
}

async function start() {
    // initWasmProto() itself now happens inside enterGame() (idempotent -- see its header comment)
    // so every path that reaches enterGame() is covered, not just this one.
    // Empty, not a hardcoded fallback -- apps/gui/main.c only ever sends a name when --name was
    // passed on the CLI, otherwise blank, letting IDUNA's own randomGuestName() assign a real
    // in-universe callsign ("Runner-A7B2" style). Hardcoding 'Runner' here silently bypassed that
    // shared generator for every guest who left the field blank (founder real-time, 2026-09-29:
    // "it needs to use account names like the windows client does random in universe names").
    const name = ($('name') as HTMLInputElement).value.trim();
    const idunaUrl = resolveIdunaUrl();
    const bridgeUrl = resolveBridgeUrl();
    const startBtn = $('start-btn') as HTMLButtonElement;
    const acctStatus = $('account-status');
    startBtn.disabled = true;
    acctStatus.textContent = 'Contacting IDUNA…';
    try {
        currentAccount = await account.bootstrapAccount(idunaUrl, name);
        acctStatus.textContent =
            'Playing as ' + currentAccount.displayName + (currentAccount.emailLinked ? ' (linked account)' : ' (guest)');
    } catch (e) {
        const msg = (e as Error).message;
        acctStatus.textContent = 'IDUNA account error: ' + msg + ' — connecting unauthenticated (only works against a --no-auth server).';
        // Real bug, found live (2026-09-28): this error only ever showed up in #account-status,
        // which the founder's own bug report never included -- so a failed bootstrap (e.g. the
        // real IDUNA guest-signup cap: 3 new accounts/IP/24h) looked identical to "the game is
        // just broken": queue-btn correctly stayed greyed out, then the connection silently died
        // ~10s later (server-side fix: expire_timers() in apps/server/main.c now sends
        // DW_ERR_AUTH instead of closing silently). Logging it here too puts the real reason in
        // the one panel that actually gets pasted into a bug report.
        log('account: ' + msg);
        currentAccount = null;
    }
    startBtn.disabled = false;
    await enterGame(idunaUrl, bridgeUrl, name);
}

// signInWithIduna is the SSO half of docs/NATIVE_WASM_CLIENT_NORTHSTAR.md item 3 -- reuses
// WOTAN/friends.html's already-live-verified pattern (see sso.ts's own header comment). Clicking
// "Sign in with IDUNA" either sends the browser to iam.okemily.com (no session yet) or, once
// IDUNA's own redirect lands back here with a token in the URL fragment, completes the exchange
// and goes straight into the game -- the click already expressed the player's intent, so there's
// no second "now click Connect" step after the redirect returns, unlike the guest flow above.
async function signInWithIduna() {
    const session = sso.handleIdunaSsoReturn() || sso.getIdunaSession();
    if (!session) {
        location.href = sso.buildSsoURL();
        return;
    }
    const idunaUrl = resolveIdunaUrl();
    const bridgeUrl = resolveBridgeUrl();
    const acctStatus = $('account-status');
    const ssoBtn = $('sso-btn') as HTMLButtonElement;
    ssoBtn.disabled = true;
    acctStatus.textContent = 'Exchanging IDUNA session for a DEADWEIGHT account…';
    try {
        currentAccount = await account.loginWithSso(idunaUrl, session.token);
        acctStatus.textContent = 'Signed in as ' + currentAccount.displayName + ' (IDUNA account)';
        await enterGame(idunaUrl, bridgeUrl, session.displayName || currentAccount.displayName);
    } catch (e) {
        sso.clearIdunaSession();
        acctStatus.textContent = 'IDUNA sign-in error: ' + (e as Error).message +
            ' — try again, or play as a guest.';
        ssoBtn.disabled = false;
    }
}

// resolveIdunaUrl/resolveBridgeUrl replace the old iduna-url/bridge-url text inputs (founder
// real-time, 2026-09-28: "we dont need IDUNA base URL or WebSocket bridge URL - we arent setting
// up for multi server right this second its just the one server"). This is a single-server
// production deploy, so both endpoints are hardcoded rather than user-configurable: same-origin
// ('' base) reaches IDUNA through the deploy's own nginx /api/ proxy (WOTAN/ops/nginx-wotan.conf),
// matching account.ts's own documented same-origin/CORS-free convention; the bridge URL matches
// ops/systemd/dw-ws-bridge.service + nginx's own /DEADWEIGHT/ws location exactly. The
// localhost/127.0.0.1 branch is the only exception, kept solely so `python3 -m http.server` local
// dev against a local dw_server + ws-tcp-bridge still works without editing source.
function resolveIdunaUrl(): string {
    if (location.hostname === 'localhost' || location.hostname === '127.0.0.1') return 'http://localhost:8080';
    return '';
}
function resolveBridgeUrl(): string {
    if (location.hostname === 'localhost' || location.hostname === '127.0.0.1') return 'ws://localhost:8765';
    const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
    return scheme + '//' + location.host + '/DEADWEIGHT/ws';
}

// A fresh SSO redirect return lands here as soon as the page (re)loads, fragment intact --
// complete the sign-in immediately rather than waiting for a click that was already made before
// leaving the page. A plain visit with only a STICKY session (no fresh fragment) does NOT
// auto-enter the game -- it just relabels the button, matching the guest flow's own "Connect"
// click requirement (see signInWithIduna's own doc comment for why fresh-return is different).
function checkStickyIdunaSession() {
    if (location.hash.includes('sso_token=')) {
        let linking = false;
        try { linking = sessionStorage.getItem(LINK_FLAG) === '1'; } catch { /* none */ }
        if (linking) { void completeLink(); return; }
        signInWithIduna();
        return;
    }
    const session = sso.getIdunaSession();
    if (session) {
        ($('sso-btn') as HTMLButtonElement).textContent = 'Continue as ' + (session.displayName || 'IDUNA account');
    }
}

// Guests get "Save progress with IDUNA": redirect through IDUNA SSO, and on return link the SSO
// identity to THIS guest account in place (guest-link-sso). A flag in sessionStorage marks the
// round trip so checkStickyIdunaSession knows to link rather than plain sign in.
const LINK_FLAG = 'dw_link_pending';
function updateSaveBtn() {
    const b = $('save-btn') as HTMLButtonElement;
    const guest = !!currentAccount && !currentAccount.emailLinked;
    b.style.display = guest ? '' : 'none';
    // A code goes to the IDUNA account it is redeemed on WOTAN with. A guest has none yet, so say how to get one first --
    // otherwise the code would land on a fresh account and not on the one being played.
    const hint = document.getElementById('redeem-hint');
    if (hint) hint.style.display = guest ? 'block' : 'none';
}
$('save-btn').addEventListener('click', () => {
    try { sessionStorage.setItem(LINK_FLAG, '1'); } catch { /* falls back to plain sign-in on return */ }
    sso.clearIdunaSession();
    location.href = sso.buildSsoURL();
});
async function completeLink(): Promise<boolean> {
    let pending = false;
    try { pending = sessionStorage.getItem(LINK_FLAG) === '1'; sessionStorage.removeItem(LINK_FLAG); } catch { /* none */ }
    if (!pending) return false;
    const session = sso.handleIdunaSsoReturn();
    if (!session) return false;
    const idunaUrl = resolveIdunaUrl();
    const acctStatus = $('account-status');
    acctStatus.textContent = 'Saving your progress to IDUNA…';
    try {
        const guest = await account.bootstrapAccount(idunaUrl, '');
        try {
            currentAccount = await account.linkSso(idunaUrl, guest, session.token);
            acctStatus.textContent = 'Progress saved — signed in as ' + currentAccount.displayName + ' (IDUNA account)';
        } catch (e) {
            if (e instanceof account.ApiError && e.status === 409) {
                currentAccount = await account.loginWithSso(idunaUrl, session.token);
                acctStatus.textContent = 'That IDUNA account already has a profile — signed in as ' + currentAccount.displayName;
            } else throw e;
        }
        await enterGame(idunaUrl, resolveBridgeUrl(), currentAccount.displayName);
    } catch (e) {
        acctStatus.textContent = 'Could not save progress: ' + (e as Error).message;
    }
    return true;
}

$('new-btn').addEventListener('click', () => {
    account.clearAccount();
    sso.clearIdunaSession();
    ($('sso-btn') as HTMLButtonElement).textContent = 'Sign in with IDUNA';
    void start();
});
$('start-btn').addEventListener('click', start);
$('sso-btn').addEventListener('click', signInWithIduna);
$('pass-btn').addEventListener('click', (ev) => { selectSlot(-1); if (ev.detail >= 2 && selectedSlot === -1) lockIn(); });
$('lockin-btn').addEventListener('click', lockIn);
checkStickyIdunaSession();
$('rename-btn').addEventListener('click', async () => {
    const idunaUrl = resolveIdunaUrl();
    const name = ($('rename-input') as HTMLInputElement).value.trim();
    const msg = $('rename-msg');
    if (!currentAccount) {
        msg.textContent = 'No account yet — click Connect first.';
        return;
    }
    if (!name || !account.isValidDisplayName(name)) {
        msg.textContent = 'Name must be 1-16 characters, no control characters.';
        return;
    }
    msg.textContent = 'Saving…';
    try {
        currentAccount = await account.setDisplayName(idunaUrl, currentAccount, name);
        ($('rename-input') as HTMLInputElement).value = '';
        msg.textContent = 'Saved.';
        updateSaveBtn();
        await initSocial();
    } catch (e) {
        msg.textContent = (e as Error).message;
    }
});
$('queue-btn').addEventListener('click', () => {
    // a draft or constructed connection queues in ITS queue: the random queue needs a mode-0 connection
    if (client.getState() !== 'ready' || connMode !== 0) {
        queueAfterReconnect = true;
        setStatus('reconnecting…');
        reconnect(0);
        $('queue-btn').setAttribute('disabled', 'true');
        return;
    }
    client.queue();
    $('queue-btn').setAttribute('disabled', 'true');
});
$('requeue').addEventListener('click', () => {
    $('requeue').style.display = 'none';
    if (connMode === 3) {   // constructed: dw_server still holds the deck submitted for this connection
        $('cons-edit').style.display = 'none';
        $('match').style.display = 'none';
        setStatus('queued (constructed)');
        client.queue(1);
        return;
    }
    ($('queue-btn') as HTMLButtonElement).disabled = false;
    client.queue();
});

// --- Friends & Duels (S537 continued) --------------------------------------------------------
// Works for ANY account (guest or email-linked) -- the IDUNA routes themselves only need a valid
// per-game player token, not a linked email; email-linking is only required on WOTAN's own
// friends.html because that's a separate static site with no other way to obtain a session here.

function escapeHtml(s: string): string {
    const d = document.createElement('div');
    d.textContent = s;
    return d.innerHTML;
}

function socialToken(): string {
    return currentAccount ? currentAccount.token : '';
}

async function initSocial() {
    if (!currentAccount) return;
    const me = $('social-me');
    try {
        const p = await social.getProfile(idunaBaseUrl, currentAccount.playerID);
        me.innerHTML =
            'You: <b>' + escapeHtml(p.display_name) + '</b> — rating ' + Math.round(p.rating) + ', ' +
            p.wins + 'W ' + p.losses + 'L ' + p.draws + 'D, ' + p.friend_count + ' friend(s)<br>' +
            '<span class="sid">Your Player ID (share this to be added): ' + escapeHtml(p.player_id) + '</span>';
    } catch (e) {
        me.textContent = 'Could not load your profile: ' + (e as Error).message;
    }
    await Promise.all([refreshRequests(), refreshFriends(), refreshDuels()]);
}

async function refreshRequests() {
    const el = $('requests-list');
    if (!currentAccount) return;
    try {
        const r = await social.listFriendRequests(idunaBaseUrl, socialToken());
        el.innerHTML = '';
        if (r.incoming.length === 0 && r.outgoing.length === 0) {
            el.textContent = 'No pending requests.';
            return;
        }
        r.incoming.forEach((fr) => {
            const row = document.createElement('div');
            row.className = 'srow';
            row.innerHTML = '<div>' + escapeHtml(fr.requester_id) + ' <i>(incoming)</i></div>';
            const actions = document.createElement('div');
            const a = document.createElement('button');
            a.textContent = 'Accept';
            a.onclick = () => respond(fr.id, true);
            const d = document.createElement('button');
            d.textContent = 'Decline';
            d.onclick = () => respond(fr.id, false);
            actions.appendChild(a);
            actions.appendChild(d);
            row.appendChild(actions);
            el.appendChild(row);
        });
        r.outgoing.forEach((fr) => {
            const row = document.createElement('div');
            row.className = 'srow';
            row.innerHTML = '<div>' + escapeHtml(fr.recipient_id) + ' <i>(outgoing, waiting)</i></div>';
            el.appendChild(row);
        });
    } catch (e) {
        el.textContent = (e as Error).message;
    }
}

async function respond(id: number, accept: boolean) {
    try {
        await social.respondFriendRequest(idunaBaseUrl, socialToken(), id, accept);
        await Promise.all([refreshRequests(), refreshFriends(), initSocial()]);
    } catch (e) {
        $('requests-list').textContent = (e as Error).message;
    }
}

async function refreshFriends() {
    const el = $('friends-list');
    if (!currentAccount) return;
    try {
        const friends = await social.listFriends(idunaBaseUrl, socialToken());
        el.innerHTML = '';
        if (friends.length === 0) {
            el.textContent = 'No friends yet.';
            return;
        }
        friends.forEach((f) => {
            const row = document.createElement('div');
            row.className = 'srow';
            row.innerHTML = '<div>' + escapeHtml(f.display_name) + ' <span class="sid">rating ' + Math.round(f.rating) + '</span></div>';
            const actions = document.createElement('div');
            const duelBtn = document.createElement('button');
            duelBtn.textContent = 'Duel';
            duelBtn.onclick = () => challengeDuel(f.player_id, duelBtn);
            const removeBtn = document.createElement('button');
            removeBtn.textContent = 'Remove';
            removeBtn.onclick = () => unfriend(f.player_id);
            actions.appendChild(duelBtn);
            actions.appendChild(removeBtn);
            row.appendChild(actions);
            el.appendChild(row);
        });
    } catch (e) {
        el.textContent = (e as Error).message;
    }
}

async function unfriend(playerID: string) {
    try {
        await social.removeFriend(idunaBaseUrl, socialToken(), playerID);
        await Promise.all([refreshFriends(), initSocial()]);
    } catch (e) {
        $('friends-list').textContent = (e as Error).message;
    }
}

async function challengeDuel(playerID: string, btn: HTMLButtonElement) {
    btn.disabled = true;
    try {
        await social.createDuel(idunaBaseUrl, socialToken(), playerID);
        await refreshDuels();
    } catch (e) {
        $('duels-list').textContent = (e as Error).message;
    } finally {
        btn.disabled = false;
    }
}

async function refreshDuels() {
    const el = $('duels-list');
    if (!currentAccount) return;
    try {
        const duels = await social.listDuels(idunaBaseUrl, socialToken());
        el.innerHTML = '';
        if (duels.length === 0) {
            el.textContent = 'No duels yet.';
            return;
        }
        duels.forEach((d) => {
            const mine = currentAccount!.playerID;
            const incoming = d.challenged_id === mine && d.status === 'pending';
            const other = d.challenger_id === mine ? d.challenged_id : d.challenger_id;
            const row = document.createElement('div');
            row.className = 'srow';
            row.innerHTML =
                '<div>' + escapeHtml(other) + ' — <b>' + escapeHtml(d.status) + '</b> ' +
                '<i>(' + (d.challenger_id === mine ? 'you challenged' : 'challenged you') + ')</i></div>';
            if (incoming) {
                const actions = document.createElement('div');
                const a = document.createElement('button');
                a.textContent = 'Accept';
                a.onclick = () => respondDuelBtn(d.id, true);
                const dec = document.createElement('button');
                dec.textContent = 'Decline';
                dec.onclick = () => respondDuelBtn(d.id, false);
                actions.appendChild(a);
                actions.appendChild(dec);
                row.appendChild(actions);
            } else if (d.status === 'accepted' && d.match_token) {
                // Live (unexpired) match_token -- IDUNA stops surfacing it once the 15-min TTL
                // lapses, matching its own stated limit, so an expired one just shows no button.
                const actions = document.createElement('div');
                const p = document.createElement('button');
                p.textContent = 'Play';
                p.onclick = () => playDuel(d.match_token!);
                actions.appendChild(p);
                row.appendChild(actions);
            }
            el.appendChild(row);
        });
    } catch (e) {
        el.textContent = (e as Error).message;
    }
}

// Duel Phase 2: queue with the duel's match_token, the same wire path an ordinary queue-btn click
// uses (encodeQueue's now-optional 3rd form), just with the token attached so dw_server's
// find_token_pair() pairs this connection with the specific friend who accepted, ahead of and
// exempt from normal FIFO/bot pairing.
function playDuel(token: string) {
    if (client && client.getState() === 'ready' && connMode === 0) {
        client.queue(0, token);
        ($('queue-btn') as HTMLButtonElement).disabled = true;
    } else {
        pendingMatchToken = token;
        setStatus('connecting… will queue for this duel once ready');
        if (connMode !== 0) reconnect(0);   // a duel is a random-queue match: leave the draft / constructed connection
    }
}

async function respondDuelBtn(id: number, accept: boolean) {
    try {
        await social.respondDuel(idunaBaseUrl, socialToken(), id, accept);
        await refreshDuels();
    } catch (e) {
        $('duels-list').textContent = (e as Error).message;
    }
}

$('add-friend-btn').addEventListener('click', async () => {
    const input = $('add-friend-id') as HTMLInputElement;
    const msg = $('add-friend-msg');
    const id = input.value.trim();
    if (!currentAccount) {
        msg.textContent = 'Connect first.';
        return;
    }
    if (!id) {
        msg.textContent = 'Player ID required.';
        return;
    }
    msg.textContent = 'Sending…';
    try {
        const body = await social.sendFriendRequest(idunaBaseUrl, socialToken(), id);
        msg.textContent = body.status === 'accepted' ? 'You are now friends.' : 'Request sent.';
        input.value = '';
        await Promise.all([refreshRequests(), refreshFriends()]);
    } catch (e) {
        msg.textContent = (e as Error).message;
    }
});


// ===================== DRAFT MODE (kanban #626) =====================
// Same flow, copy and button set as apps/gui/main.c's S_DRAFT / S_DRAFT_HUB screens. The IDUNA ticket spend
// and run state go through account.ts's draftRun* (the routes dwi_draft_run_* call); the wire frames go
// through the same wasm codec as everything else. Layout is DOM/CSS, not the SDL pixel grid -- see README.

const DRAFT_PICKS = 16;

function draftCardBox(id: number): string {
    const c = cardsData.cards[id];
    const kind = rules.cardKind(id);
    return `<div class="card-header" style="background:${KIND_COLORS[kind]};">${c ? c.name : '#' + id}</div>` +
        `<div class="card-body"><div class="card-meta">Cost ${c ? c.cost : '?'}${c && c.credit ? ' +' + c.credit + 'cr' : ''} · Pwr ${c ? c.power : '?'}</div>` +
        `<div class="card-kind">${KIND_NAMES[kind]}${c && c.keyword ? ' / ' + c.keyword : ''}</div>` +
        (c && c.text ? `<span class="rules-text">${c.text}</span>` : '') + `</div>`;
}

function showScreen(which: 'game' | 'draft' | 'hub' | 'constructed') {
    $('draft').style.display = which === 'draft' ? 'block' : 'none';
    $('hub').style.display = which === 'hub' ? 'block' : 'none';
    $('constructed').style.display = which === 'constructed' ? 'block' : 'none';
    if (which !== 'game') $('match').style.display = 'none';   // game: leave #match as the match events set it
    ($('queue-btn') as HTMLElement).style.display = which === 'game' ? '' : 'none';
    ($('draft-btn') as HTMLElement).style.display = which === 'game' ? '' : 'none';
    ($('cons-btn') as HTMLElement).style.display = which === 'game' ? '' : 'none';
}

// enableDraft=false is the "just look again" refresh (returning to this tab): it updates the label, the FOUNDER badge and
// the Draft button's wording, but never changes whether the button is enabled -- the caller is mid-whatever.
async function refreshTickets(enableDraft = true) {
    if (!currentAccount) return;
    try {
        const st = await account.ticketStatus(idunaBaseUrl, currentAccount.playerID);
        const n = st.tickets;
        $('ticket-count').textContent = `TICKETS: ${n}`;
        // The native client's gold FOUNDER mark (apps/gui/main.c draw_menu), shown for as long as the account is one.
        // optional-chained like the other Redeem/Founder bits below: a stale cached index.html next to a fresh main.js (no
        // Cache-Control, so heuristic caching can pair them) must lose the badge, never the whole client
        document.getElementById('founder-badge')?.classList.toggle('on', st.founder);
        ($('draft-btn') as HTMLButtonElement).textContent = n > 0 ? 'Draft (cost: 1 ticket)' : 'Draft (no tickets)';
        ($('draft-btn') as HTMLButtonElement).dataset.tickets = String(n);
        // The button ships disabled in index.html and nothing used to enable it after connect.
        // Enable it unless a run is mid-start/active (startDraftRun/openHub manage those cases).
        if (enableDraft && !draftActive) ($('draft-btn') as HTMLButtonElement).disabled = false;
    } catch { /* cosmetic */ }
}

// A code redeemed on WOTAN changes the balance and the Founder flag on the server. The "Redeem a code" link opens WOTAN in a
// NEW tab (so this tab's connection and match survive), which means the answer arrives while this tab is in the background:
// look again the moment it is looked at, no reload. visibilitychange covers a tab switch, focus covers two windows side by
// side; the 2 s floor keeps one return from asking twice.
let lastTicketLook = 0;
function lookAtTicketsOnReturn() {
    if (document.hidden || !currentAccount) return;
    const now = Date.now();
    if (now - lastTicketLook < 2000) return;
    lastTicketLook = now;
    void refreshTickets(false);
}
document.addEventListener('visibilitychange', lookAtTicketsOnReturn);
window.addEventListener('focus', lookAtTicketsOnReturn);

// WOTAN's Redeem a Code page. Same origin in production (this client is served from wotan.okemily.com/DEADWEIGHT/); the
// localhost dev server has no such page, so it points at production.
function redeemUrl(): string {
    if (location.hostname === 'localhost' || location.hostname === '127.0.0.1') return 'https://wotan.okemily.com/redeem/';
    return '/redeem/';
}
for (const id of ['redeem-link', 'redeem-link-setup']) {
    const a = document.getElementById(id) as HTMLAnchorElement | null;
    if (a) a.href = redeemUrl();
}

function renderDraft() {
    const pickNo = draftPicks.length;
    $('draft-title').textContent = `DRAFT  PICK ${Math.min(pickNo + 1, DRAFT_PICKS)}/${DRAFT_PICKS}`;
    $('draft-left').textContent = `LEFT  1X: ${draftLeft[0]}   2X: ${draftLeft[1]}   3X: ${draftLeft[2]}`;
    const offer = $('draft-offer');
    offer.innerHTML = '';
    for (let c = 0; c < 2; c++) {
        const col = document.createElement('div');
        const box = document.createElement('div');
        box.className = 'draft-card';
        box.innerHTML = draftOfferCards[c] >= 0 ? draftCardBox(draftOfferCards[c]) : '';
        col.appendChild(box);
        const mults = document.createElement('div');
        mults.className = 'draft-mults';
        for (let m = 1; m <= 3; m++) {
            const b = document.createElement('button');
            b.textContent = `${m}X`;
            b.disabled = draftLeft[m - 1] <= 0;
            b.onclick = () => {
                draftPend = { card: draftOfferCards[c], mult: m };
                client.draftPick(c as 0 | 1, m as 1 | 2 | 3);
            };
            mults.appendChild(b);
        }
        col.appendChild(mults);
        offer.appendChild(col);
    }
    $('draft-deck-title').textContent = `YOUR DECK SO FAR (${draftPicks.length})`;
    $('draft-deck').innerHTML = draftPicks.map((p) => `${p.mult}X ${escapeHtml(cardsData.cards[p.card] ? cardsData.cards[p.card].name : '#' + p.card)}`).join('<br>');
}

function draftOffer(f: { pickNo: number; total: number; card: number[]; left: number[] }) {
    if (f.pickNo === 0) draftPicks = [];
    else if (f.pickNo === draftPicks.length + 1 && draftPicks.length < DRAFT_PICKS) draftPicks.push({ card: draftPend.card, mult: draftPend.mult });
    draftOfferCards = [f.card[0], f.card[1]];
    draftLeft = [f.left[0], f.left[1], f.left[2]];
    setStatus('drafting');
    showScreen('draft');
    renderDraft();
}

async function draftDone(f: { deckId: number; cards: number[] }) {
    if (connMode === 3) {   // the server took the deck we submitted: queue with it (QUEUE does not auto-follow DRAFT_RESUME)
        showScreen('game');
        setStatus(`queued (constructed deck ${f.deckId})`);
        client.queue(1);
        return;
    }
    draftDeck = f.cards.slice();
    draftDeckId = f.deckId;
    showScreen('game');
    setStatus('queued (draft deck ' + f.deckId + ')');
    if (draftAwaitingResumeQueue) {
        draftAwaitingResumeQueue = false;
        client.queue(1);
    } else if (currentAccount) {
        // A genuinely new deck: persist it so a boot-time resume / the Hub has a server-side record.
        // Fire-and-forget -- an IDUNA outage never blocks the match (same contract as the desktop client).
        account.draftRunSaveDeck(idunaBaseUrl, currentAccount.token, draftDeck).catch((e) => log('draft deck save failed: ' + (e as Error).message));
    }
}

function reconnect(mode: 0 | 2 | 3) {
    if (client) client.close();
    connMode = mode;
    client = new DeadweightClient(connBridge, clientHandlers);
    client.connect(connName, connToken, mode);
}

// ===================== CONSTRUCTED MODE =====================
function openBuilder() {
    if (!consBuilder) return;
    showScreen('constructed');
    consBuilder.open();
    // the server pairs you with the bot whose ladder rating is nearest yours: show it (cosmetic, like the rings)
    consBuilder.setRating(null);
    if (currentAccount) {
        social.getProfile(idunaBaseUrl, currentAccount.playerID).then((p) => consBuilder && consBuilder.setRating(p.rating)).catch(() => { /* cosmetic */ });
    }
}

function startConstructed(cards: number[]) {
    consDeckInPlay = cards.slice();
    showScreen('game');
    $('match').style.display = 'none';
    $('requeue').style.display = 'none';
    $('cons-edit').style.display = 'none';
    ($('queue-btn') as HTMLButtonElement).disabled = true;
    log(`constructed deck: ${cards.length} cards`);
    if (client && client.getState() === 'ready' && connMode === 3) {
        client.draftResume(cards);   // already connected in constructed mode (between matches)
        setStatus('submitting deck…');
        return;
    }
    consWantCards = cards.slice();
    setStatus('connecting (constructed)…');
    reconnect(3);
}

$('cons-btn').addEventListener('click', () => openBuilder());
$('cons-edit').addEventListener('click', () => {
    $('requeue').style.display = 'none';
    $('cons-edit').style.display = 'none';
    if (consBuilder && consDeckInPlay.length) consBuilder.load(consDeckInPlay);
    openBuilder();
});

async function startDraftRun() {
    const msg = $('draft-msg');
    if (!currentAccount) { msg.textContent = 'No account (IDUNA offline)'; return; }
    msg.textContent = '';
    ($('draft-btn') as HTMLButtonElement).disabled = true;
    try {
        const r = await account.draftRunStart(idunaBaseUrl, currentAccount.token);
        refreshTickets();
        draftActive = true;
        if (!r.ticketSpent) {
            // resuming an already-active run: straight to the Hub, never re-spend
            await openHub(false);
            return;
        }
        draftDeck = []; draftPicks = [];
        draftWantQueue = true;
        reconnect(2);
        showScreen('game');
        setStatus('connecting (draft)…');
    } catch (e) {
        const err = e as { status?: number; message: string };
        msg.textContent = err.status === 402 ? 'Insufficient tickets' : 'IDUNA unreachable: ' + err.message;
        ($('draft-btn') as HTMLButtonElement).disabled = false;
    }
}

async function openHub(connected: boolean) {
    if (!currentAccount) return;
    let st: account.DraftRunState | null = null;
    try { st = await account.draftRunState(idunaBaseUrl, currentAccount.token); } catch { /* shown below */ }
    // S520: trust the server's state -- a 3rd loss auto-cashes-out and flips active=false.
    if (st && !st.active) {
        draftActive = false;
        $('hub-msg').textContent = '';
        showScreen('game');
        ($('draft-btn') as HTMLButtonElement).disabled = false;
        ($('requeue').style.display = 'inline-block');
        refreshTickets();
        setStatus('draft run over');
        return;
    }
    const wins = st ? st.wins : 0, losses = st ? st.losses : 0;
    const deck = st && st.deck.length ? st.deck : draftDeck;
    draftDeck = deck.slice();
    $('hub-wins').textContent = `WINS: ${wins}`;
    Array.from($('hub-proxies').children).forEach((el, i) => el.classList.toggle('burned', i < losses));
    $('hub-deck-title').textContent = `DECK  (${deck.length} CARDS)`;
    $('hub-deck').innerHTML = deck.slice(0, 23).map((id) => escapeHtml(cardsData.cards[id] ? cardsData.cards[id].name : '#' + id)).join('<br>');
    $('hub-msg').textContent = st ? '' : 'IDUNA unreachable (showing last known deck)';
    ($('hub-resume') as HTMLElement).dataset.connected = connected ? '1' : '0';
    showScreen('hub');
}

$('draft-btn').addEventListener('click', () => { void startDraftRun(); });

$('hub-resume').addEventListener('click', () => {
    showScreen('game');
    if (client && client.getState() === 'ready') {
        client.queue(1);   // still connected (came from MATCH_END): dw_server holds this deck in memory
        ($('queue-btn') as HTMLButtonElement).disabled = true;
    } else {
        draftResumeCards = draftDeck.slice();
        reconnect(2);
    }
    setStatus('resuming uplink…');
});

$('hub-abort').addEventListener('click', async () => {
    if (!currentAccount) return;
    try {
        const r = await account.draftRunAbort(idunaBaseUrl, currentAccount.token);
        draftActive = false;
        draftDeck = [];
        $('draft-msg').textContent = `Extracted: ${r.wins} win${r.wins === 1 ? '' : 's'} -> +${r.granted} ticket${r.granted === 1 ? '' : 's'}`;
        showScreen('game');
        ($('draft-btn') as HTMLButtonElement).disabled = false;
        reconnect(0);
        refreshTickets();
    } catch (e) {
        $('hub-msg').textContent = 'Abort failed (' + (e as Error).message + ')';
    }
});

$('draft-leave').addEventListener('click', async () => {
    // Mid-draft LEAVE (desktop: send LEAVE, back to menu). The run's ticket was already spent, so refund via
    // abort exactly like start_draft_run()'s connect-failure path: a 0-win/0-loss run is a full refund.
    try { client.leave(); } catch { /* socket may be gone */ }
    showScreen('game');
    if (currentAccount && draftActive && draftDeck.length === 0) {
        try { await account.draftRunAbort(idunaBaseUrl, currentAccount.token); } catch { /* best effort */ }
        draftActive = false;
    }
    ($('draft-btn') as HTMLButtonElement).disabled = false;
    reconnect(0);
    refreshTickets();
});
