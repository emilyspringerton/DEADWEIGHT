// DEADWEIGHT browser client — UI glue. Hand-written host (this file + index.html), PARENA is the
// decision layer (generated/CardRules.ts — isLegalPlay, cardKind, cardKeyword, etc. are called
// directly, never re-implemented here), same "PARENA is the trigger, host does the work" idiom
// every other DEADWEIGHT client already follows (Android Java shell, Windows SDL2 GUI).

import { DeadweightClient } from './client.js';
import * as rules from './generated/CardRules.js';
import * as fx from './fx.js';
import * as account from './account.js';

type CardEntry = { id: number; name: string; kind: string; keyword: string; cost: number; power: number; credit: number; text: string };
type CardsData = { version: string; cards: CardEntry[] };

const KIND_NAMES = ['Offense', 'Operations', 'Defense'];
const KIND_COLORS = ['#c0392b', '#d4a017', '#2f6fb0'];

const $ = (id: string) => document.getElementById(id)!;

let client: DeadweightClient;
let currentAccount: account.Account | null = null;
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
    $('setup').style.display = 'none';
    $('game').style.display = 'block';

    cardsData = await (await fetch('./src/generated/cards.json')).json();
    log(`loaded ${cardsData.cards.length}-card catalog (v${cardsData.version})`);

    client = new DeadweightClient(bridgeUrl, {
        onState(s) {
            setStatus(s);
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
    client.connect(currentAccount ? currentAccount.displayName : name, currentAccount ? currentAccount.token : '');
}

$('start-btn').addEventListener('click', start);
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
        currentAccount = await account.linkEmail(idunaUrl, currentAccount, email, password);
        msg.textContent = 'Linked — you can sign in with this email on WOTAN’s Friends & Duels page too.';
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
