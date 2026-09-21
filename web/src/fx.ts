// DEADWEIGHT browser round-resolution animation/audio -- the DECISION layer (which scenario, who
// won, was it a critical, when each stage starts) comes from web/src/generated/FxRules.ts, the
// exact same PARENA-compiled logic apps/gui/fx.c now calls instead of hand-deriving (see
// core/fx_rules.h's own header comment; founder real-time 2026-09-21: "windows is canonical, TS
// should copy it exactly... dogfood it, eat more of the app"). What's hand-written here, on
// purpose, matching that same "PARENA decides, host renders" idiom every DEADWEIGHT client
// follows: actually drawing (Canvas2D, not SDL2 rectangles) and actually synthesizing audio (Web
// Audio oscillators, not the C software synth) -- a genuinely different renderer making the same
// decisions, not a byte-for-byte visual clone (SDL2 pixels can't be reproduced in a browser
// without shipping the SDL2 renderer itself).
import * as fx from './generated/FxRules.js';
import * as rules from './generated/CardRules.js';

export interface FxRoundInput {
    cardYou: number; cardOpp: number; effYou: number; effOpp: number;
    cancelledYou: boolean; cancelledOpp: boolean;
    dmgYou: number; dmgOpp: number; healYou: number; healOpp: number;
    armorBeforeYou: number; armorAfterYou: number; armorBeforeOpp: number; armorAfterOpp: number;
    vaultBeforeYou: number; vaultAfterYou: number; vaultBeforeOpp: number; vaultAfterOpp: number;
    /** 0 when unknown -- the wire protocol has no direct "energy delta" field; the caller derives
     * this from consecutive ROUND_START.energyYou values (best-effort, matching fx.c's own
     * FX_UNKNOWN handling for the same real gap: the server doesn't send an energy delta either). */
    energyDeltaYou: number; energyDeltaOpp: number;
    /** best-effort: whether either side's status bitmask newly gained burn/regen since the LAST
     * ROUND_START (docs/WIRE_PROTOCOL.md has no status field on ROUND_RESULT itself -- status is
     * only ever visible via the next ROUND_START -- so this is one round's lag behind the C
     * client's own status_after/status_before, an honest, named gap, not a silent guess). */
    newStatusYou: boolean; newStatusOpp: boolean;
    disabledYou: boolean; disabledOpp: boolean; swapped: boolean;
}

export interface FxTimeline {
    scenario: number; win: number; lose: number; crit: boolean; kw: number; clashCue: number;
    clash0: number; clash1: number; hull0: number; hull1: number; armor0: number; armor1: number;
    econ0: number; econ1: number; stat0: number; stat1: number; settle0: number; totalMs: number;
    cardYouId: number; cardOppId: number; kindYou: number; kindOpp: number;
}

export function computeTimeline(i: FxRoundInput): FxTimeline {
    const cardYouId = fx.fxCardId(i.effYou, i.cardYou, i.cancelledYou ? 1 : 0);
    const cardOppId = fx.fxCardId(i.effOpp, i.cardOpp, i.cancelledOpp ? 1 : 0);
    const kindYou = fx.fxKindOf(cardYouId);
    const kindOpp = fx.fxKindOf(cardOppId);
    const ca = i.cancelledYou ? 1 : 0, cb = i.cancelledOpp ? 1 : 0;
    const scenario = fx.fxScenario(kindYou, kindOpp, ca, cb);
    const win = fx.fxWinnerSeat(kindYou, kindOpp, ca, cb);
    const lose = win >= 0 ? 1 - win : -1;
    let crit = false, kw = 0;
    if (win >= 0 && lose >= 0) {
        const winId = win === 0 ? cardYouId : cardOppId;
        const loseId = lose === 0 ? cardYouId : cardOppId;
        if (scenario === 3) kw = rules.cardKeyword(winId);
        const dmgLose = lose === 0 ? i.dmgYou : i.dmgOpp;
        crit = fx.fxIsCrit(scenario, dmgLose, rules.cardPower(winId), rules.cardPower(loseId), rules.cardCost(winId));
    }
    const clashCue = fx.fxClashCue(scenario, crit, kw);
    const clashDur = fx.fxClashDurationMs(scenario, crit);
    const hasHull = fx.fxHasHull(i.dmgYou, i.dmgOpp, i.healYou, i.healOpp) ? 1 : 0;
    const hasArmor = fx.fxHasArmor(i.armorBeforeYou, i.armorAfterYou, i.armorBeforeOpp, i.armorAfterOpp) ? 1 : 0;
    const vdYou = i.vaultAfterYou - i.vaultBeforeYou, vdOpp = i.vaultAfterOpp - i.vaultBeforeOpp;
    const hasEcon = fx.fxHasEcon(i.energyDeltaYou, i.energyDeltaOpp, vdYou, vdOpp) ? 1 : 0;
    const hasStat = fx.fxHasStat(i.newStatusYou ? 1 : 0, i.newStatusOpp ? 1 : 0, i.disabledYou ? 1 : 0, i.disabledOpp ? 1 : 0, i.swapped ? 1 : 0) ? 1 : 0;
    return {
        scenario, win, lose, crit, kw, clashCue,
        clash0: fx.fxClash0(), clash1: fx.fxClash1(clashDur),
        hull0: fx.fxHull0(clashDur), hull1: fx.fxHull1(clashDur, hasHull),
        armor0: fx.fxArmor0(clashDur, hasHull), armor1: fx.fxArmor1(clashDur, hasHull, hasArmor),
        econ0: fx.fxEcon0(clashDur, hasHull, hasArmor), econ1: fx.fxEcon1(clashDur, hasHull, hasArmor, hasEcon),
        stat0: fx.fxStat0(clashDur, hasHull, hasArmor, hasEcon), stat1: fx.fxStat1(clashDur, hasHull, hasArmor, hasEcon, hasStat),
        settle0: fx.fxSettle0(clashDur, hasHull, hasArmor, hasEcon, hasStat),
        totalMs: fx.fxTimelineTotalMs(clashDur, hasHull, hasArmor, hasEcon, hasStat),
        cardYouId, cardOppId, kindYou, kindOpp,
    };
}

const SCENARIO_NAME = ['none', 'blitz', 'block', 'bypass', 'mirror_offense', 'mirror_operations', 'mirror_defense', 'unopposed', 'hold_shield', 'hold', 'cancelled'];
const KW_NAME = ['', 'lock', 'sabotage', 'flank', 'scan', 'siphon'];
const KIND_COLOR = ['#e05252', '#d4b23a', '#4a90d9'];

export function scenarioName(t: FxTimeline): string {
    return SCENARIO_NAME[t.scenario] + (t.scenario === 3 ? '_' + KW_NAME[t.kw] : '') + (t.crit ? '_crit' : '');
}

// --- Web Audio: a small, structurally-matching (not sample-identical) synth. Red = noise burst +
// low thump, Blue = a slow pad chord, Yellow = a fast ascending square arpeggio -- same
// per-kind timbre assignment docs/ANIMATION_AND_AUDIO.md documents for the C client, built with
// oscillators/noise buffers instead of the C software synth's own voice mixer. ---
let actx: AudioContext | null = null;
function ctx(): AudioContext {
    if (!actx) actx = new (window.AudioContext || (window as any).webkitAudioContext)();
    return actx;
}

function noiseBurst(at: number, dur: number, pan: number, gain: number) {
    const c = ctx();
    const buf = c.createBuffer(1, Math.floor(c.sampleRate * dur), c.sampleRate);
    const d = buf.getChannelData(0);
    for (let i = 0; i < d.length; i++) d[i] = (Math.random() * 2 - 1) * (1 - i / d.length);
    const src = c.createBufferSource(); src.buffer = buf;
    const g = c.createGain(); g.gain.value = gain;
    const p = c.createStereoPanner(); p.pan.value = pan;
    src.connect(g).connect(p).connect(c.destination);
    src.start(c.currentTime + at / 1000);
}

function tone(at: number, dur: number, freq: number, type: OscillatorType, pan: number, gain: number) {
    const c = ctx();
    const osc = c.createOscillator(); osc.type = type; osc.frequency.value = freq;
    const g = c.createGain();
    g.gain.setValueAtTime(0, c.currentTime + at / 1000);
    g.gain.linearRampToValueAtTime(gain, c.currentTime + at / 1000 + 0.02);
    g.gain.exponentialRampToValueAtTime(0.001, c.currentTime + at / 1000 + dur / 1000);
    const p = c.createStereoPanner(); p.pan.value = pan;
    osc.connect(g).connect(p).connect(c.destination);
    osc.start(c.currentTime + at / 1000);
    osc.stop(c.currentTime + at / 1000 + dur / 1000 + 0.05);
}

function playClashCue(cue: number, at: number, pan: number, crit: boolean) {
    // Cue ids match sfx.h's own SfxCue enum order (see FxRules.ts's fxClashCue). Grouped by
    // family rather than all 17 individually voiced -- a real, honest, coarser v0, not a claim of
    // 1:1 parity with the C synth's per-cue tuning.
    if (cue === 0 || cue === 1) { // BLITZ / BLITZ_CRIT
        noiseBurst(at, 0.35, pan, crit ? 0.9 : 0.6);
        tone(at, 0.25, 70, 'square', pan, 0.5);
    } else if (cue === 2 || cue === 3) { // BLOCK / BLOCK_CRIT
        tone(at, 0.9, 220, 'sine', pan, 0.35);
        tone(at + 40, 0.9, 330, 'sine', pan, 0.25);
        tone(at + 300, 0.3, 660, 'triangle', pan, 0.2);
    } else if (cue >= 4 && cue <= 9) { // BYPASS + keyword variants + BYPASS_CRIT
        for (let i = 0; i < 5; i++) tone(at + i * 55, 0.12, 500 + i * 180, 'square', pan, 0.18);
        tone(at + 5 * 55 + 40, 0.3, 1400, 'square', pan, 0.25);
    } else if (cue === 16) { // IMMUNE / hold_shield
        tone(at, 0.5, 440, 'sine', pan, 0.3);
    } else if (cue === 15) { // CANCELLED
        tone(at, 0.2, 150, 'sawtooth', pan, 0.2);
    } else { // mirrors, unopposed, hold, default
        tone(at, 0.3, 300, 'triangle', pan, 0.2);
    }
}

function playResourceBlip(at: number, kind: 'hull' | 'armor' | 'energy' | 'credits' | 'heal', pan: number, intensity: number) {
    const freq = kind === 'hull' ? 90 : kind === 'armor' ? 260 : kind === 'energy' ? 520 : kind === 'credits' ? 700 : 880;
    const type: OscillatorType = kind === 'hull' ? 'square' : kind === 'heal' ? 'sine' : 'triangle';
    tone(at, kind === 'hull' ? 0.25 : 0.4, freq, type, pan, 0.15 + 0.15 * Math.min(intensity, 1.5));
}

// --- Canvas2D visuals: a simplified but real timeline renderer, timed off the same stage
// boundaries fx.c's own SDL2 renderer uses (t.clash0/clash1/hull0/... above). One call per
// animation frame; the caller (main.ts) drives the requestAnimationFrame loop. ---
export interface FxDrawState { startedAt: number; timeline: FxTimeline; input: FxRoundInput; audioScheduled: boolean }

export function beginRound(canvas: HTMLCanvasElement, timeline: FxTimeline, input: FxRoundInput): FxDrawState {
    const state: FxDrawState = { startedAt: performance.now(), timeline, input, audioScheduled: false };
    scheduleAudio(timeline, input);
    return state;
}

function scheduleAudio(t: FxTimeline, i: FxRoundInput) {
    try {
        playClashCue(t.clashCue, t.clash0 + 60, t.win === 0 ? -0.4 : t.win === 1 ? 0.4 : 0, t.crit);
        if (i.dmgYou > 0) playResourceBlip(t.hull0 + 60, 'hull', -0.5, i.dmgYou / 12);
        if (i.dmgOpp > 0) playResourceBlip(t.hull0 + 60, 'hull', 0.5, i.dmgOpp / 12);
        if (i.healYou > 0) playResourceBlip(t.hull0 + 300, 'heal', -0.5, 1);
        if (i.healOpp > 0) playResourceBlip(t.hull0 + 300, 'heal', 0.5, 1);
        if (i.armorAfterYou > i.armorBeforeYou) playResourceBlip(t.armor0 + 60, 'armor', -0.5, 1);
        if (i.armorAfterOpp > i.armorBeforeOpp) playResourceBlip(t.armor0 + 60, 'armor', 0.5, 1);
        if (i.energyDeltaYou > 0 || i.vaultAfterYou > i.vaultBeforeYou) playResourceBlip(t.econ0 + 60, i.energyDeltaYou > 0 ? 'energy' : 'credits', -0.5, 1);
        if (i.energyDeltaOpp > 0 || i.vaultAfterOpp > i.vaultBeforeOpp) playResourceBlip(t.econ0 + 60, i.energyDeltaOpp > 0 ? 'energy' : 'credits', 0.5, 1);
    } catch (e) {
        // Web Audio can throw (autoplay policy: no user gesture yet) -- the animation still runs
        // silently rather than crashing the round-resolution flow.
    }
}

const SHIP_DX = 120;

export function drawFrame(ctx2d: CanvasRenderingContext2D, w: number, h: number, state: FxDrawState): boolean {
    const t = performance.now() - state.startedAt;
    const tl = state.timeline;
    ctx2d.clearRect(0, 0, w, h);
    ctx2d.fillStyle = '#0c0e12'; ctx2d.fillRect(0, 0, w, h);

    const cx = w / 2, cy = h / 2;
    const kindColorYou = KIND_COLOR[tl.kindYou >= 0 ? tl.kindYou : 1];
    const kindColorOpp = KIND_COLOR[tl.kindOpp >= 0 ? tl.kindOpp : 1];
    const flip = Math.min(t / 600, 1);
    const shipScale = 0.6 + 0.4 * flip;

    drawShip(ctx2d, cx - SHIP_DX, cy, -1, shipScale, kindColorYou);
    drawShip(ctx2d, cx + SHIP_DX, cy, 1, shipScale, kindColorOpp);

    if (t >= tl.clash0 && t < tl.clash1) drawClash(ctx2d, cx, cy, tl, t - tl.clash0, tl.clash1 - tl.clash0);
    else if (t >= tl.clash1) drawResultLabel(ctx2d, cx, cy, tl);

    drawStageLabels(ctx2d, cx, cy, tl, t, state.input);

    return t < tl.totalMs;
}

function drawShip(c: CanvasRenderingContext2D, x: number, y: number, dir: number, scale: number, color: string) {
    c.save();
    c.translate(x, y);
    c.scale(dir * scale, scale);
    c.beginPath();
    c.moveTo(34, 0); c.lineTo(6, -10); c.lineTo(-6, -22); c.lineTo(-24, -22); c.lineTo(-14, -6);
    c.lineTo(-30, -6); c.lineTo(-30, 6); c.lineTo(-14, 6); c.lineTo(-24, 22); c.lineTo(-6, 22); c.lineTo(6, 10);
    c.closePath();
    c.fillStyle = '#20232c'; c.fill();
    c.strokeStyle = color; c.lineWidth = 2; c.stroke();
    c.restore();
}

function drawClash(c: CanvasRenderingContext2D, cx: number, cy: number, tl: FxTimeline, t: number, dur: number) {
    const k = t / dur;
    if (tl.scenario === 1) { // BLITZ: missiles from winner toward loser
        const fromX = tl.win === 0 ? cx - SHIP_DX : cx + SHIP_DX;
        const toX = tl.lose === 0 ? cx - SHIP_DX : cx + SHIP_DX;
        for (let i = 0; i < 5; i++) {
            const mk = Math.min(Math.max(k * 1.6 - i * 0.08, 0), 1);
            const mx = fromX + (toX - fromX) * mk, my = cy + (i - 2) * 8;
            c.fillStyle = tl.crit ? '#ff6a3a' : '#e05252';
            c.beginPath(); c.arc(mx, my, 3 + (tl.crit ? 1 : 0), 0, 7); c.fill();
        }
        if (k > 0.7) { c.fillStyle = `rgba(255,150,60,${0.5 * (1 - (k - 0.7) / 0.3)})`; c.beginPath(); c.arc(toX, cy, 40 * (k - 0.7) / 0.3, 0, 7); c.fill(); }
    } else if (tl.scenario === 2) { // BLOCK: shield hex expands
        const shieldX = tl.win === 0 ? cx - SHIP_DX : cx + SHIP_DX;
        c.strokeStyle = `rgba(74,144,217,${0.8 - 0.3 * k})`; c.lineWidth = 3;
        c.beginPath(); c.arc(shieldX, cy, 30 + 20 * Math.min(k * 2, 1), 0, 7); c.stroke();
    } else if (tl.scenario === 3) { // BYPASS: rising arpeggio dots toward the loser's shield
        const shieldX = tl.lose === 0 ? cx - SHIP_DX : cx + SHIP_DX;
        c.fillStyle = '#d4b23a';
        for (let i = 0; i < 6; i++) {
            const dk = Math.min(Math.max(k * 1.4 - i * 0.1, 0), 1);
            c.globalAlpha = dk;
            c.beginPath(); c.arc(shieldX - 40 + i * 14, cy - 30 + dk * 30, 4, 0, 7); c.fill();
        }
        c.globalAlpha = 1;
    }
}

function drawResultLabel(c: CanvasRenderingContext2D, cx: number, cy: number, tl: FxTimeline) {
    const labels: Record<number, string> = {
        1: 'INTERRUPT', 2: 'ABSORBED', 3: ['BYPASS', 'LOCKED', 'SABOTAGE', 'FLANKED', 'EXPOSED', 'SIPHON'][tl.kw],
        4: 'STALEMATE', 5: 'STALEMATE', 6: 'STALEMATE', 7: 'UNOPPOSED', 8: 'HOLD', 9: 'BOTH HOLD +1 ENERGY', 10: 'CANCELLED',
    };
    const label = labels[tl.scenario];
    if (!label) return;
    c.font = 'bold 20px monospace';
    c.fillStyle = tl.scenario === 1 ? '#e05252' : tl.scenario === 2 ? '#4a90d9' : tl.scenario === 3 ? '#d4b23a' : '#aaa';
    c.textAlign = 'center';
    c.fillText(label, cx, cy - 60);
}

function drawStageLabels(c: CanvasRenderingContext2D, cx: number, cy: number, tl: FxTimeline, t: number, i: FxRoundInput) {
    c.font = '14px monospace'; c.textAlign = 'center';
    if (t >= tl.hull0 && t < tl.hull1 + 300) {
        if (i.dmgYou > 0) popText(c, cx - SHIP_DX, cy + 40, `-${i.dmgYou}`, '#e05252', t - tl.hull0);
        if (i.dmgOpp > 0) popText(c, cx + SHIP_DX, cy + 40, `-${i.dmgOpp}`, '#e05252', t - tl.hull0);
        if (i.healYou > 0) popText(c, cx - SHIP_DX, cy + 40, `+${i.healYou}`, '#4ad97a', t - tl.hull0);
        if (i.healOpp > 0) popText(c, cx + SHIP_DX, cy + 40, `+${i.healOpp}`, '#4ad97a', t - tl.hull0);
    }
    if (t >= tl.armor0 && t < tl.armor1 + 300) {
        const dYou = i.armorAfterYou - i.armorBeforeYou, dOpp = i.armorAfterOpp - i.armorBeforeOpp;
        if (dYou !== 0) popText(c, cx - SHIP_DX, cy + 60, `${dYou > 0 ? '+' : ''}${dYou} ARMOR`, dYou > 0 ? '#c0c0c0' : '#7a9c4a', t - tl.armor0);
        if (dOpp !== 0) popText(c, cx + SHIP_DX, cy + 60, `${dOpp > 0 ? '+' : ''}${dOpp} ARMOR`, dOpp > 0 ? '#c0c0c0' : '#7a9c4a', t - tl.armor0);
    }
}

function popText(c: CanvasRenderingContext2D, x: number, y: number, text: string, color: string, age: number) {
    const rise = Math.min(age / 900, 1) * -20;
    const alpha = age < 700 ? 1 : Math.max(0, 1 - (age - 700) / 200);
    c.globalAlpha = alpha;
    c.fillStyle = color;
    c.fillText(text, x, y + rise);
    c.globalAlpha = 1;
}
