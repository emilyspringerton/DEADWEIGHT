// DEADWEIGHT browser client — UI glue. Hand-written host (this file + index.html), PARENA is the
// decision layer (generated/CardRules.ts — isLegalPlay, cardKind, cardKeyword, etc. are called
// directly, never re-implemented here), same "PARENA is the trigger, host does the work" idiom
// every other DEADWEIGHT client already follows (Android Java shell, Windows SDL2 GUI).

import { DeadweightClient } from './client.js';
import { initWasmProto } from './wasmProto.js';
import type { RoundStart } from './wasmProto.js';
import * as rules from './generated/CardRules.js';
import * as fx from './fx.js';
import * as account from './account.js';
import * as social from './social.js';
import * as sso from './sso.js';

type CardEntry = { id: number; name: string; kind: string; keyword: string; cost: number; power: number; credit: number; text: string };
type CardsData = { version: string; cards: CardEntry[] };

// Exact match to apps/gui/main.c's KIND_NAME/KIND_COL (Windows/Linux desktop client) --
// docs/BRAND_STYLE_GUIDE.md Section 2A is the single source of truth for both clients.
const KIND_NAMES = ['OFFENSE', 'OPERATIONS', 'DEFENSE'];
const KIND_COLORS = ['#D7463C', '#E1A028', '#468CE6'];

const $ = (id: string) => document.getElementById(id)!;

let client: DeadweightClient;
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
// "before" snapshot for the round about to resolve, captured at ROUND_START, consumed by the next
// ROUND_RESULT's fx.computeTimeline() call -- see fx.ts's own header comment for the honest,
// named gap this leaves (energy-delta and burn/regen status visuals are not wired up yet, the
// wire protocol doesn't carry enough same-round information for either without deferring a round).
let beforeArmorYou = 0, beforeArmorOpp = 0, beforeVaultYou = 0, beforeVaultOpp = 0;
let fxDrawState: fx.FxDrawState | null = null;
let fxRafHandle = 0;
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
            btn.onclick = () => selectSlot(slot);
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

function pipsHtml(energy: number, hidden: boolean): string {
    if (hidden) return '<span style="opacity:0.7;">? (hidden)</span>';
    let html = '';
    for (let i = 0; i < 6; i++) html += `<span class="epip${i < energy ? ' full' : ''}"></span>`;
    return html;
}

function renderBars(f: RoundStart) {
    ($('round-num-big') as HTMLElement).textContent = `Round ${f.round}/${rules.maxRounds()}`;

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

function setStatus(s: string) {
    $('status').textContent = s;
}

function runFxAnimation(timeline: fx.FxTimeline, input: fx.FxRoundInput) {
    const canvas = $('fx-canvas') as HTMLCanvasElement;
    const ctx2d = canvas.getContext('2d')!;
    if (fxRafHandle) cancelAnimationFrame(fxRafHandle);
    fxDrawState = fx.beginRound(canvas, timeline, input);
    const step = () => {
        if (!fxDrawState) return;
        const stillRunning = fx.drawFrame(ctx2d, canvas.width, canvas.height, fxDrawState);
        if (stillRunning) fxRafHandle = requestAnimationFrame(step);
    };
    fxRafHandle = requestAnimationFrame(step);
}

// enterGame is start()'s own real tail, extracted (2026-09-25) so createAccount() below can
// reach the exact same "connected and playing" state without duplicating the client wiring --
// both paths only differ in HOW currentAccount got resolved (bootstrap vs. a fresh
// register+claim), never in what happens once it's resolved.
async function enterGame(idunaUrl: string, bridgeUrl: string, fallbackName: string) {
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

    if (currentAccount) initSocial();

    cardsData = await (await fetch('./src/generated/cards.json')).json();
    log(`loaded ${cardsData.cards.length}-card catalog (v${cardsData.version})`);

    client = new DeadweightClient(bridgeUrl, {
        onState(s) {
            setStatus(s);
            const queueBtn = $('queue-btn') as HTMLButtonElement;
            if (s === 'ready' && pendingMatchToken) {
                const tok = pendingMatchToken;
                pendingMatchToken = null;
                client.queue(0, tok);
                queueBtn.disabled = true;
            } else {
                // Only 'ready' means the server has actually WELCOMEd this connection -- every
                // other state (connecting/queued/in_match/error/closed) must keep this disabled,
                // see enterGame's own header comment on the ERROR-4 race this closes.
                queueBtn.disabled = s !== 'ready';
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
            log(`MATCH_FOUND vs ${f.oppName} (${f.oppKind === 1 ? 'bot' : 'human'}), seat ${f.seat}, seed ${f.seed}`);
            ($('opp-name') as HTMLElement).textContent = `${f.oppName} (${f.oppKind === 1 ? 'bot' : 'human'})`;
            $('match').style.display = 'block';
        },
        onRoundStart(f) {
            currentHand = f.hand;
            currentEnergy = f.energyYou;
            currentVault = f.vaultYou;
            lockMask = f.lockMask;
            locked = false;
            selectedSlot = null;
            beforeArmorYou = f.armorYou; beforeArmorOpp = f.armorOpp;
            beforeVaultYou = f.vaultYou; beforeVaultOpp = f.vaultOpp;
            renderBars(f);
            renderHand();
            updateActionButtons();
            log(`round ${f.round} start: hull ${f.hullYou}/${f.hullOpp}, energy ${f.energyYou}, vault ${f.vaultYou}`);
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
            log(`round ${f.round} result: you played ${cardLabel(f.cardYou)}, opp played ${cardLabel(f.cardOpp)} — dealt ${f.dmgToOpp}, took ${f.dmgToYou}, hull now ${f.hullYou}/${f.hullOpp}`);
            const input: fx.FxRoundInput = {
                cardYou: f.cardYou, cardOpp: f.cardOpp, effYou: f.effYou, effOpp: f.effOpp,
                cancelledYou: !!(f.flagsYou & 1), cancelledOpp: !!(f.flagsOpp & 1),
                dmgYou: f.dmgToYou, dmgOpp: f.dmgToOpp, healYou: f.healYou, healOpp: f.healOpp,
                armorBeforeYou: beforeArmorYou, armorAfterYou: f.armorYou,
                armorBeforeOpp: beforeArmorOpp, armorAfterOpp: f.armorOpp,
                vaultBeforeYou: beforeVaultYou, vaultAfterYou: f.vaultYou,
                vaultBeforeOpp: beforeVaultOpp, vaultAfterOpp: f.vaultOpp,
                energyDeltaYou: 0, energyDeltaOpp: 0, // honest gap -- see fx.ts's own header comment
                newStatusYou: false, newStatusOpp: false, // honest gap -- see fx.ts's own header comment
                disabledYou: !!(f.flagsOpp & 8), disabledOpp: !!(f.flagsYou & 8),
                swapped: !!((f.flagsYou & 16) || (f.flagsOpp & 16)),
            };
            const timeline = fx.computeTimeline(input);
            log(`fx: scenario=${fx.scenarioName(timeline)} win=${timeline.win} crit=${timeline.crit} total=${Math.round(timeline.totalMs)}ms`);
            runFxAnimation(timeline, input);
        },
        onMatchEnd(f) {
            const outcome = f.result === 1 ? 'WIN' : f.result === 0 ? 'LOSS' : 'DRAW';
            log(`MATCH_END: ${outcome} (reason ${f.reason})`);
            setStatus(`match over: ${outcome}`);
            $('requeue').style.display = 'inline-block';
        },
        onError(f) {
            log(`ERROR code ${f.code}`);
        },
    });
    client.connect(currentAccount ? currentAccount.displayName : fallbackName, currentAccount ? currentAccount.token : '');
}

async function start() {
    await initWasmProto(); // must resolve before client.connect() ever calls into the wasm codec
    const name = ($('name') as HTMLInputElement).value.trim() || 'Runner';
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
        ($('link-email-box') as HTMLElement).style.display = currentAccount.emailLinked ? 'none' : 'block';
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
        ($('link-email-box') as HTMLElement).style.display = 'none';
        await enterGame(idunaUrl, bridgeUrl, session.displayName || currentAccount.displayName);
    } catch (e) {
        sso.clearIdunaSession();
        acctStatus.textContent = 'IDUNA sign-in error: ' + (e as Error).message +
            ' — no DEADWEIGHT account is linked to this IDUNA identity yet. Play as a guest below, ' +
            'then use "Link an email" with the same email/password to connect it.';
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
        signInWithIduna();
        return;
    }
    const session = sso.getIdunaSession();
    if (session) {
        ($('sso-btn') as HTMLButtonElement).textContent = 'Continue as ' + (session.displayName || 'IDUNA account');
    }
}

// createAccount is the real "Create Account" flow (2026-09-25, founder real-time: "it should let
// me create a deadweight account right there with a button"): one button, real, live client-side
// name feedback (account.isValidDisplayName, mirroring IDUNA's own server-side check -- see its
// own doc comment for the honest PARENA-TS-emitter gap this hand-written copy works around),
// register-with-a-chosen-name + immediately claim with email/password, then straight into the
// game exactly like the existing guest-then-Connect path does.
async function createAccount() {
    const name = ($('create-name') as HTMLInputElement).value.trim();
    const email = ($('create-email') as HTMLInputElement).value.trim();
    const password = ($('create-password') as HTMLInputElement).value;
    const idunaUrl = resolveIdunaUrl();
    const bridgeUrl = resolveBridgeUrl();
    const createBtn = $('create-account-btn') as HTMLButtonElement;
    const msg = $('create-account-msg');
    if (!name || !account.isValidDisplayName(name)) {
        msg.textContent = 'Name must be 1-16 characters, no control characters.';
        return;
    }
    if (!email || !password) {
        msg.textContent = 'Email and password required.';
        return;
    }
    if (password.length < 8) {
        msg.textContent = 'Password needs 8+ characters.';
        return;
    }
    createBtn.disabled = true;
    msg.textContent = 'Creating…';
    try {
        currentAccount = await account.registerAndClaim(idunaUrl, name, email, password);
        msg.textContent = '';
        await enterGame(idunaUrl, bridgeUrl, name);
    } catch (e) {
        msg.textContent = (e as Error).message;
        createBtn.disabled = false;
    }
}

$('start-btn').addEventListener('click', start);
$('sso-btn').addEventListener('click', signInWithIduna);
$('create-account-btn').addEventListener('click', createAccount);
$('pass-btn').addEventListener('click', () => selectSlot(-1));
$('lockin-btn').addEventListener('click', lockIn);
checkStickyIdunaSession();
$('link-btn').addEventListener('click', async () => {
    const idunaUrl = resolveIdunaUrl();
    const email = ($('link-email') as HTMLInputElement).value.trim();
    const password = ($('link-password') as HTMLInputElement).value;
    const msg = $('link-msg');
    if (!currentAccount) {
        msg.textContent = 'No account yet — click Connect first.';
        return;
    }
    msg.textContent = 'Linking…';
    try {
        const before = currentAccount.playerID;
        currentAccount = await account.claimOrLogin(idunaUrl, currentAccount, email, password);
        msg.textContent = currentAccount.playerID === before
            ? 'Linked — you can sign in with this email on WOTAN’s Friends & Duels page too.'
            : 'This email already had an account — signed in to it instead (your fresh guest session is unused, not lost).';
        ($('link-email-box') as HTMLElement).style.display = 'none';
        ($('account-status') as HTMLElement).textContent = 'Playing as ' + currentAccount.displayName + ' (linked account)';
    } catch (e) {
        msg.textContent = (e as Error).message;
    }
});
$('queue-btn').addEventListener('click', () => {
    client.queue();
    $('queue-btn').setAttribute('disabled', 'true');
});
$('requeue').addEventListener('click', () => {
    $('requeue').style.display = 'none';
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
    if (client && client.getState() === 'ready') {
        client.queue(0, token);
        ($('queue-btn') as HTMLButtonElement).disabled = true;
    } else {
        pendingMatchToken = token;
        setStatus('connecting… will queue for this duel once ready');
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
