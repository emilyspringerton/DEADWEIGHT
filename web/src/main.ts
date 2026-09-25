// DEADWEIGHT browser client — UI glue. Hand-written host (this file + index.html), PARENA is the
// decision layer (generated/CardRules.ts — isLegalPlay, cardKind, cardKeyword, etc. are called
// directly, never re-implemented here), same "PARENA is the trigger, host does the work" idiom
// every other DEADWEIGHT client already follows (Android Java shell, Windows SDL2 GUI).

import { DeadweightClient } from './client.js';
import * as rules from './generated/CardRules.js';
import * as fx from './fx.js';
import * as account from './account.js';
import * as social from './social.js';

type CardEntry = { id: number; name: string; kind: string; keyword: string; cost: number; power: number; credit: number; text: string };
type CardsData = { version: string; cards: CardEntry[] };

const KIND_NAMES = ['Offense', 'Operations', 'Defense'];
const KIND_COLORS = ['#c0392b', '#d4a017', '#2f6fb0'];

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

function renderHand() {
    const handEl = $('hand');
    handEl.innerHTML = '';
    currentHand.forEach((id, slot) => {
        const btn = document.createElement('button');
        btn.className = 'card-btn';
        if (id === -1) {
            btn.textContent = '(empty slot)';
            btn.disabled = true;
        } else {
            const c = cardsData.cards[id];
            const kind = rules.cardKind(id);
            const legal = rules.isLegalPlay(id, currentEnergy, currentVault) && !((lockMask >> slot) & 1);
            btn.style.borderColor = KIND_COLORS[kind];
            btn.innerHTML = `<b>${c ? c.name : '#' + id}</b><br><small>${KIND_NAMES[kind]}${c && c.keyword ? ' / ' + c.keyword : ''}</small><br>` +
                `<small>cost ${c ? c.cost : '?'}${c && c.credit ? ' +' + c.credit + 'cr' : ''} · pow ${c ? c.power : '?'}</small>` +
                (c && c.text ? `<br><small class="rules-text">${c.text}</small>` : '');
            btn.disabled = !legal || locked;
            btn.onclick = () => {
                locked = true;
                renderHand();
                client.play(slot);
                log(`locked slot ${slot}: ${cardLabel(id)}`);
            };
        }
        handEl.appendChild(btn);
    });

    const passBtn = document.createElement('button');
    passBtn.className = 'card-btn pass-btn';
    passBtn.textContent = 'PASS (+1 energy)';
    passBtn.disabled = locked;
    passBtn.onclick = () => {
        locked = true;
        renderHand();
        client.play(-1);
        log('locked: PASS');
    };
    handEl.appendChild(passBtn);
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

    if (currentAccount) initSocial();

    cardsData = await (await fetch('./src/generated/cards.json')).json();
    log(`loaded ${cardsData.cards.length}-card catalog (v${cardsData.version})`);

    client = new DeadweightClient(bridgeUrl, {
        onState(s) {
            setStatus(s);
            if (s === 'ready' && pendingMatchToken) {
                const tok = pendingMatchToken;
                pendingMatchToken = null;
                client.queue(0, tok);
                ($('queue-btn') as HTMLButtonElement).disabled = true;
            }
        },
        onLog(line) {
            log(line);
        },
        onQueued(waiting) {
            setStatus(`queued (${waiting} waiting)`);
        },
        onMatchFound(f) {
            log(`MATCH_FOUND vs ${f.oppName} (${f.oppKind === 1 ? 'bot' : 'human'}), seat ${f.seat}, seed ${f.seed}`);
            $('match').style.display = 'block';
        },
        onRoundStart(f) {
            currentHand = f.hand;
            currentEnergy = f.energyYou;
            currentVault = f.vaultYou;
            lockMask = f.lockMask;
            locked = false;
            beforeArmorYou = f.armorYou; beforeArmorOpp = f.armorOpp;
            beforeVaultYou = f.vaultYou; beforeVaultOpp = f.vaultOpp;
            $('round-num').textContent = String(f.round);
            $('hull-you').textContent = String(f.hullYou);
            $('hull-opp').textContent = String(f.hullOpp);
            $('energy-you').textContent = String(f.energyYou);
            $('vault-you').textContent = String(f.vaultYou);
            $('armor-you').textContent = String(f.armorYou);
            renderHand();
            log(`round ${f.round} start: hull ${f.hullYou}/${f.hullOpp}, energy ${f.energyYou}, vault ${f.vaultYou}`);
        },
        onPlayReject(f) {
            locked = false;
            renderHand();
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
    const name = ($('name') as HTMLInputElement).value.trim() || 'BrowserPlayer';
    const idunaUrl = ($('iduna-url') as HTMLInputElement).value.trim();
    const bridgeUrl = ($('bridge-url') as HTMLInputElement).value.trim();
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
        acctStatus.textContent = 'IDUNA account error: ' + (e as Error).message + ' — connecting unauthenticated (only works against a --no-auth server).';
        currentAccount = null;
    }
    startBtn.disabled = false;
    await enterGame(idunaUrl, bridgeUrl, name);
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
    const idunaUrl = ($('iduna-url') as HTMLInputElement).value.trim();
    const bridgeUrl = ($('bridge-url') as HTMLInputElement).value.trim();
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
$('create-account-btn').addEventListener('click', createAccount);
$('link-btn').addEventListener('click', async () => {
    const idunaUrl = ($('iduna-url') as HTMLInputElement).value.trim();
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
