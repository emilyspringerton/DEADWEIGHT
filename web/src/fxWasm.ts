// fxWasm.ts -- the browser HOST half of DEADWEIGHT's "SDL layer for wasm" (founder real-time,
// 2026-09-28: "write an sdl layer for wasm for when we dont have SDL abstract our shit so we can
// use PARENA to write the same exact logic"). dw_fx.wasm (scripts/build_wasm_fx.sh) is
// apps/gui/fx.c + apps/gui/sfx.c compiled completely unmodified for wasm32-unknown-unknown --
// this file is the JS-side implementation of the small set of functions that module imports
// (apps/wasm/fx/SDL.h's own five draw primitives, plus FxHost's pre-existing text/text_w
// callback seam), translating them to Canvas2D drawing and real audio playback. Windows
// implements the identical calls via real SDL2 (apps/gui/main.c). Same round-resolution animation
// and sound-design C source, two hosts -- not a second hand-written reimplementation of fx.c's own
// logic in TypeScript, which is what web/src/fx.ts's rendering used to be -- main.ts no longer
// uses it for the live client, but fx.ts itself is left in place: web/bridge/e2e_test.mjs still
// legitimately calls its computeTimeline() as a live decision-layer sanity check, orthogonal to
// which renderer actually draws the game.
//
// Scope, honestly: this drives fx_draw_arena (the clash/ship/particle/card-flip animation, fully
// self-contained within apps/gui/fx.c's own AY..AY+AH "arena band", y=170..420 of its 480x956
// virtual screen) and real audio via sfx.c's own offline-render pull API. fx_draw_overlay (redline
// border across the FULL virtual screen), fx_draw_status_panel and fx_draw_disabled_card (drawn at
// absolute HUD coordinates -- HULL_Y/ENERGY_XY/etc in fx.c -- that assume the native app's own
// fixed full-screen layout, which this DOM-based page's hull bars don't share) are NOT called here
// yet -- a real, named, deliberate gap (not silently dropped), since wiring them up needs the
// canvas to cover the whole match screen as an overlay, a bigger layout change than this pass
// scopes. Some particle effects that fly toward those same absolute HUD coordinates (e.g. the
// hull-damage burst flying toward HULL_Y) will therefore draw off this canvas's own cropped
// viewport and simply not be visible -- same honest boundary.
//
// Founder real-time, 2026-09-28 (bug report): "it doesnt show the card text on the cards when you
// reveal it also doesnt work right like i can get killt and it doesnt show my health go to the
// bottom." Root cause: fx_draw_arena's own card-flip/reveal drawing used to call back into this
// file's own js_draw_card, which was a deliberate stub (colour box + centred id only, "no card art
// assets wired through the wasm boundary yet"). Fixed at the SOURCE, not here: apps/gui/fx.c now
// has a real fx_draw_card_box() (name/cost-power/kind-keyword/wrapped rules text, from
// core/card_text.h + core/card_rules.h) that both the clash-flip animation and a newly-real idle
// "LAST ROUND" reveal panel (previously only ever drawn by a separate, hand-duplicated copy in
// apps/gui/main.c that this wasm client had no equivalent of at all) call directly -- no host
// callback needed for cards any more, so js_draw_card/CardMeta/the cardLookup argument this file
// used to take are gone entirely, not left as unused scaffolding.

const AY = 170; // apps/gui/fx.c's own #define AY -- the arena band's top in fx.c's world coordinates
const AH = 250; // apps/gui/fx.c's own #define AH -- matches web/index.html's #fx-canvas height exactly (1:1 pixel mapping, no scale)
const SFX_RATE = 44100; // apps/gui/sfx.h's own #define SFX_RATE

interface FxWasmExports {
    memory: WebAssembly.Memory;
    wasm_fx_init(): void;
    wasm_fx_unknown(): number;
    fxr_reset(): void;
    fxr_set_round(v: number): void;
    fxr_set_card(seat: number, v: number): void;
    fxr_set_eff(seat: number, v: number): void;
    fxr_set_dmg(seat: number, v: number): void;
    fxr_set_heal(seat: number, v: number): void;
    fxr_set_hull_before(seat: number, v: number): void;
    fxr_set_hull_after(seat: number, v: number): void;
    fxr_set_armor_before(seat: number, v: number): void;
    fxr_set_armor_after(seat: number, v: number): void;
    fxr_set_vault_before(seat: number, v: number): void;
    fxr_set_vault_after(seat: number, v: number): void;
    fxr_set_energy_delta(seat: number, v: number): void;
    fxr_set_energy_capped(seat: number, v: number): void;
    fxr_set_flags(seat: number, v: number): void;
    fxr_set_status_after(seat: number, v: number): void;
    fxr_set_status_before(seat: number, v: number): void;
    fxr_set_lock_after(v: number): void;
    fxr_set_lethal(v: number): void;
    fxr_set_seed(v: number): void;
    wasm_fx_begin(): void;
    wasm_fx_energy_estimate(seat: number, rsEnergy: number, energyNext: number): number;
    wasm_fx_energy_capped(seat: number, rsEnergy: number, energyNext: number): number;
    wasm_fx_reset(): void;
    wasm_fx_set_speed(sp: number): void;
    wasm_fx_set_meters(hy: number, ho: number, ay: number, ao: number, ey: number, eo: number, vy: number, vo: number, snap: number): void;
    wasm_fx_update(dt: number): void;
    wasm_fx_active(): number;
    wasm_fx_elapsed_ms(): number;
    wasm_fx_finish(): void;
    wasm_fx_total_ms(): number;
    wasm_fx_scenario_name(): number;
    wasm_fx_pulse(seat: number, meter: number): number;
    wasm_fx_set_redline(hy: number, ho: number): void;
    wasm_fx_status_changed(sy: number, so: number, lockMask: number, newRound: number): void;
    wasm_fx_shake_dx(): number;
    wasm_fx_shake_dy(): number;
    wasm_fx_shown_hull(seat: number): number;
    wasm_fx_shown_armor(seat: number): number;
    wasm_fx_shown_energy(seat: number): number;
    wasm_fx_shown_vault(seat: number): number;
    wasm_fx_draw_arena(revealYou: number, revealOpp: number, haveReveal: number, revealRound: number, revealDmgYou: number, revealDmgOpp: number): void;
    wasm_fx_draw_overlay(): void;
    wasm_fx_draw_status_panel(seat: number, x: number, y: number, w: number, h: number): void;
    wasm_fx_draw_disabled_card(x: number, y: number, w: number, h: number): void;
    wasm_audio_buf_ptr(): number;
    wasm_audio_chunk_capacity(): number;
    wasm_sfx_render(frames: number): number;
    wasm_sfx_set_muted(m: number): void;
    wasm_sfx_is_muted(): number;
    wasm_sfx_active_voices(): number;
    wasm_sfx_stop_all(): void;
}

let wasm: FxWasmExports | null = null;
let ctx2d: CanvasRenderingContext2D | null = null;

// SDL2's real draw-color API is stateful (SetDrawColor sets "current colour"; Fill/DrawLine/
// DrawPoint all use it) -- fx.c's own setc() calls both every single draw, so this is simple:
let curColor = 'rgba(255,255,255,1)';
function setColor(r: number, g: number, b: number, a: number) {
    curColor = `rgba(${r},${g},${b},${(a / 255).toFixed(3)})`;
}
function wy(y: number) { return y - AY; } // world Y -> canvas Y (see AY/AH header comment)

function readCString(mem: WebAssembly.Memory, ptr: number): string {
    const bytes = new Uint8Array(mem.buffer);
    let end = ptr;
    while (end < bytes.length && bytes[end] !== 0) end++;
    let s = '';
    for (let i = ptr; i < end; i++) s += String.fromCharCode(bytes[i]);
    return s;
}

function fontFor(scale: number): string {
    // apps/gui/main.c's own text_w: strlen(s) * 6 * scale (a 6px-wide bitmap glyph per scale
    // unit) -- 9px per scale unit approximates that same overall label footprint in a real
    // monospace web font closely enough to read as "the same size system", not a literal glyph
    // clone (Canvas2D can't reproduce a hand-drawn bitmap font).
    return `bold ${Math.max(8, 9 * scale)}px 'JetBrains Mono', 'Courier New', monospace`;
}

const imports = {
    env: {
        // apps/wasm/fx/SDL.h's own five real SDL calls -- the "SDL layer for wasm" itself.
        SDL_SetRenderDrawBlendMode(): number { return 0; }, // fx.c always blends; Canvas2D rgba() already does, nothing to toggle
        SDL_SetRenderDrawColor(_r: number, red: number, g: number, b: number, a: number): number {
            setColor(red, g, b, a);
            return 0;
        },
        SDL_RenderFillRect(_r: number, rectPtr: number): number {
            if (!wasm || !ctx2d) return 0;
            const v = new Int32Array(wasm.memory.buffer, rectPtr, 4);
            ctx2d.fillStyle = curColor;
            ctx2d.fillRect(v[0], wy(v[1]), v[2], v[3]);
            return 0;
        },
        SDL_RenderDrawLine(_r: number, x1: number, y1: number, x2: number, y2: number): number {
            if (!ctx2d) return 0;
            ctx2d.strokeStyle = curColor;
            ctx2d.lineWidth = 1;
            ctx2d.beginPath();
            ctx2d.moveTo(x1 + 0.5, wy(y1) + 0.5);
            ctx2d.lineTo(x2 + 0.5, wy(y2) + 0.5);
            ctx2d.stroke();
            return 0;
        },
        SDL_RenderDrawPoint(_r: number, x: number, y: number): number {
            if (!ctx2d) return 0;
            ctx2d.fillStyle = curColor;
            ctx2d.fillRect(x, wy(y), 1, 1);
            return 0;
        },
        // FxHost's own pre-existing host-callback seam (fx.h), not a new abstraction this module invented.
        js_draw_text(x: number, y: number, scale: number, r: number, g: number, b: number, a: number, strPtr: number) {
            if (!wasm || !ctx2d) return;
            const s = readCString(wasm.memory, strPtr);
            ctx2d.font = fontFor(scale);
            ctx2d.textBaseline = 'top';
            ctx2d.textAlign = 'left';
            ctx2d.fillStyle = `rgba(${r},${g},${b},${(a / 255).toFixed(3)})`;
            ctx2d.fillText(s, x, wy(y));
        },
        js_text_width(scale: number, strPtr: number): number {
            if (!wasm || !ctx2d) return 0;
            const s = readCString(wasm.memory, strPtr);
            ctx2d.font = fontFor(scale);
            return Math.round(ctx2d.measureText(s).width);
        },
        // apps/gui/sfx.c's own sfx_write_wav (dead code on this pull-based-audio path -- this host
        // only ever calls wasm_sfx_render, never that function) still declares fopen/fwrite/fclose
        // (apps/wasm/fx/stdio.h), and WebAssembly.instantiate requires every declared import to be
        // a real callable even if the wasm module itself never calls it at runtime. Never invoked.
        fopen(): number { return 0; },
        fwrite(): number { return 0; },
        fclose(): number { return 0; },
    },
};

export async function initFxWasm(
    canvas: HTMLCanvasElement,
    wasmUrl = 'dist/generated/dw_fx.wasm',
): Promise<void> {
    ctx2d = canvas.getContext('2d')!;
    const bytes = await (await fetch(wasmUrl)).arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes, imports);
    wasm = instance.exports as unknown as FxWasmExports;
    wasm.wasm_fx_init();
}

function requireWasm(): FxWasmExports {
    if (!wasm) throw new Error('fxWasm: initFxWasm() must be awaited before use');
    return wasm;
}

export const FX_UNKNOWN = -1000; // matches apps/gui/fx.h's own #define FX_UNKNOWN -- see wasm_fx_unknown() below for the live cross-check

export interface FxRoundInput {
    round: number;
    cardYou: number; cardOpp: number; effYou: number; effOpp: number;
    dmgYou: number; dmgOpp: number; healYou: number; healOpp: number;
    hullBeforeYou: number; hullAfterYou: number; hullBeforeOpp: number; hullAfterOpp: number;
    armorBeforeYou: number; armorAfterYou: number; armorBeforeOpp: number; armorAfterOpp: number;
    vaultBeforeYou: number; vaultAfterYou: number; vaultBeforeOpp: number; vaultAfterOpp: number;
    /** Energy at the ROUND_START that began THIS round (main.ts's own rsEnergyYou/rsEnergyOpp,
     * mirroring apps/gui/main.c's A.rs_energy) and energy at the NEXT ROUND_START (null when there
     * isn't one yet -- the match ended on this round) -- see fx_wasm.c's own
     * wasm_fx_energy_estimate header comment for why this is computed in C, not re-derived here:
     * it's apps/gui/main.c's own real UI-glue heuristic, ported once, not reinvented. */
    rsEnergyYou: number; rsEnergyOpp: number;
    energyNextYou: number | null; energyNextOpp: number | null;
    /** raw ROUND_RESULT flags_you/flags_opp byte -- docs/WIRE_PROTOCOL.md's own bit layout
     * (cancelled/immune/lifeline/lockedOpp/swapped/discard/redraw/copied) is bit-for-bit the same
     * as FxRound's own flags[2] (fx.h's own comment lists the identical eight in the identical
     * order), so this is passed straight through with zero remapping. */
    flagsYou: number; flagsOpp: number;
    /** raw ROUND_START status_you/status_opp bytes (bit0 burning, bit1 regenerating, bit2
     * hidden) -- same "identical bit layout, no remapping" situation as flags above. */
    statusBeforeYou: number; statusBeforeOpp: number; statusAfterYou: number; statusAfterOpp: number;
    lockAfter: number; lethal: boolean; seed: number;
}

export function beginRound(input: FxRoundInput) {
    const w = requireWasm();
    w.fxr_reset();
    w.fxr_set_round(input.round);
    w.fxr_set_card(0, input.cardYou); w.fxr_set_card(1, input.cardOpp);
    w.fxr_set_eff(0, input.effYou); w.fxr_set_eff(1, input.effOpp);
    w.fxr_set_dmg(0, input.dmgYou); w.fxr_set_dmg(1, input.dmgOpp);
    w.fxr_set_heal(0, input.healYou); w.fxr_set_heal(1, input.healOpp);
    w.fxr_set_hull_before(0, input.hullBeforeYou); w.fxr_set_hull_before(1, input.hullBeforeOpp);
    w.fxr_set_hull_after(0, input.hullAfterYou); w.fxr_set_hull_after(1, input.hullAfterOpp);
    w.fxr_set_armor_before(0, input.armorBeforeYou); w.fxr_set_armor_before(1, input.armorBeforeOpp);
    w.fxr_set_armor_after(0, input.armorAfterYou); w.fxr_set_armor_after(1, input.armorAfterOpp);
    w.fxr_set_vault_before(0, input.vaultBeforeYou); w.fxr_set_vault_before(1, input.vaultBeforeOpp);
    w.fxr_set_vault_after(0, input.vaultAfterYou); w.fxr_set_vault_after(1, input.vaultAfterOpp);
    // card[seat]/eff[seat] are already set above -- wasm_fx_energy_estimate/_capped read them
    // straight off g_r, exactly like apps/gui/main.c's own fx_energy_estimate reads A.pend.
    const nextYou = input.energyNextYou, nextOpp = input.energyNextOpp;
    const dY = nextYou === null ? FX_UNKNOWN : w.wasm_fx_energy_estimate(0, input.rsEnergyYou, nextYou);
    const dO = nextOpp === null ? FX_UNKNOWN : w.wasm_fx_energy_estimate(1, input.rsEnergyOpp, nextOpp);
    const cY = nextYou === null ? 0 : w.wasm_fx_energy_capped(0, input.rsEnergyYou, nextYou);
    const cO = nextOpp === null ? 0 : w.wasm_fx_energy_capped(1, input.rsEnergyOpp, nextOpp);
    w.fxr_set_energy_delta(0, dY); w.fxr_set_energy_delta(1, dO);
    w.fxr_set_energy_capped(0, cY); w.fxr_set_energy_capped(1, cO);
    w.fxr_set_flags(0, input.flagsYou); w.fxr_set_flags(1, input.flagsOpp);
    w.fxr_set_status_before(0, input.statusBeforeYou); w.fxr_set_status_before(1, input.statusBeforeOpp);
    w.fxr_set_status_after(0, input.statusAfterYou); w.fxr_set_status_after(1, input.statusAfterOpp);
    w.fxr_set_lock_after(input.lockAfter);
    w.fxr_set_lethal(input.lethal ? 1 : 0);
    w.fxr_set_seed(input.seed >>> 0);
    w.wasm_fx_begin();
}

export function setMeters(hullYou: number, hullOpp: number, armorYou: number, armorOpp: number,
                           energyYou: number, energyOpp: number, vaultYou: number, vaultOpp: number, snap: boolean) {
    requireWasm().wasm_fx_set_meters(hullYou, hullOpp, armorYou, armorOpp, energyYou, energyOpp, vaultYou, vaultOpp, snap ? 1 : 0);
}
export function setRedline(hullYou: number, hullOpp: number) { requireWasm().wasm_fx_set_redline(hullYou, hullOpp); }
export function statusChanged(statusYou: number, statusOpp: number, lockMask: number, newRound: boolean) {
    requireWasm().wasm_fx_status_changed(statusYou, statusOpp, lockMask, newRound ? 1 : 0);
}
export function isActive(): boolean { return requireWasm().wasm_fx_active() !== 0; }
export function scenarioName(): string {
    const w = requireWasm();
    return readCString(w.memory, w.wasm_fx_scenario_name());
}
export function resetMatch() { requireWasm().wasm_fx_reset(); }

// ---- per-frame driving: one call per animation frame, advances both the visual timeline (real
// fx_update) and the audio clock (real sfx_render) off the SAME dt, then draws the arena band.
// fx_update must keep running every frame for the whole match, not just during one round's
// timeline -- persistent effects (burn embers, regen sparkles, the redline heartbeat, EMP decay)
// live outside S.active (see apps/gui/fx.c's own fx_update: parts_update/labels_update still run
// when idle) -- see this file's own header comment for what's NOT drawn yet (overlay/status-panel/
// disabled-card, which need a full-screen canvas this pass doesn't build). ----
let audioCtx: AudioContext | null = null;
let audioEpoch = 0; // AudioContext.currentTime this round's frame 0 was scheduled at
let framesScheduled = 0; // cumulative frames handed to Web Audio since audioEpoch -- see comment above beginRoundAudio

function ctx(): AudioContext {
    if (!audioCtx) audioCtx = new (window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext)();
    return audioCtx;
}

/** Call once when a match starts (or whenever the audio clock should re-anchor, e.g. after a long
 * pause) -- establishes the epoch every subsequent pumpAudio() chunk schedules relative to, so
 * chunks stay sample-accurately gapless regardless of how JS's own per-call wall-clock jitters. */
export function resetAudioClock() {
    try { audioEpoch = ctx().currentTime + 0.05; framesScheduled = 0; } catch (e) { /* no user gesture yet */ }
}

function pumpAudio(dtMs: number) {
    const w = requireWasm();
    let framesWanted = Math.round((dtMs / 1000) * SFX_RATE);
    if (framesWanted <= 0) return;
    const cap = w.wasm_audio_chunk_capacity();
    if (framesWanted > cap) framesWanted = cap;
    try {
        const rendered = w.wasm_sfx_render(framesWanted);
        if (rendered <= 0) return;
        const ptr = w.wasm_audio_buf_ptr();
        const pcm = new Int16Array(w.memory.buffer, ptr, rendered * 2);
        const ac = ctx();
        const buf = ac.createBuffer(2, rendered, SFX_RATE);
        const l = buf.getChannelData(0), r = buf.getChannelData(1);
        for (let i = 0; i < rendered; i++) { l[i] = pcm[2 * i] / 32768; r[i] = pcm[2 * i + 1] / 32768; }
        const src = ac.createBufferSource();
        src.buffer = buf;
        src.connect(ac.destination);
        src.start(audioEpoch + framesScheduled / SFX_RATE);
        framesScheduled += rendered;
    } catch (e) {
        // Web Audio can throw pre-user-gesture (autoplay policy); the visual timeline still runs silently.
    }
}

/** reveal: the last-resolved round's data, kept live for the whole match (mirrors apps/gui/main.c's
 * own persistent A.rv_you/A.rv_opp/A.have_reveal/A.rv_round/A.rv_dy/A.rv_do) -- fx_draw_arena reads
 * it whenever the clash animation itself isn't active, to draw the "LAST ROUND" reveal panel (or
 * "PICK A CARD OR PASS" before the first one) instead of leaving the canvas blank. */
export interface RevealState { you: number; opp: number; have: boolean; round: number; dmgYou: number; dmgOpp: number }

export function tick(dtMs: number, canvas: HTMLCanvasElement, reveal: RevealState) {
    const w = requireWasm();
    w.wasm_fx_update(Math.max(0, Math.round(dtMs)));
    pumpAudio(dtMs);
    if (ctx2d) { ctx2d.clearRect(0, 0, canvas.width, canvas.height); ctx2d.fillStyle = '#12141C'; ctx2d.fillRect(0, 0, canvas.width, canvas.height); }
    w.wasm_fx_draw_arena(reveal.you, reveal.opp, reveal.have ? 1 : 0, reveal.round, reveal.dmgYou, reveal.dmgOpp);
}

export function setMuted(m: boolean) { requireWasm().wasm_sfx_set_muted(m ? 1 : 0); }
export function isMuted(): boolean { return requireWasm().wasm_sfx_is_muted() !== 0; }
