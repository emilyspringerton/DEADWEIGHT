/* Real wasm export boundary around DEADWEIGHT's actual round-resolution animation/audio engine
 * (apps/gui/fx.c + apps/gui/sfx.c, compiled into this module completely unmodified -- same "eat
 * the codebase, don't reimplement it a third time" discipline apps/wasm/protocol_wasm.c's own
 * header comment already states for the wire codec). Scalar getter/setter exports only, never raw
 * struct/memory poking from JS -- same reasoning protocol_wasm.c gives (a struct-layout change
 * would silently break raw poking, never a named setter). The genuinely new part here, versus
 * protocol_wasm.c (which needs zero imports): fx.c/sfx.c need a real host to draw pixels and play
 * audio, so this module DOES import a small set of JS-provided functions -- SDL_SetRenderDrawColor
 * etc. (declared in apps/wasm/fx/SDL.h, the "SDL layer for wasm" itself) plus js_draw_text/
 * js_text_width (FxHost's own existing host-callback seam, already an abstraction point in fx.h
 * before this module ever existed). web/src/fxWasm.ts implements both by drawing to a Canvas2D 2d
 * context; apps/gui/main.c implements the identical calls via real SDL2 and its own bitmap font.
 * Card drawing itself needs no host callback at all: fx_draw_card_box() (apps/gui/fx.c) draws a
 * whole card -- name, cost/power, kind/keyword, wrapped rules text -- from only the primitives
 * above plus core/card_text.h's card data, so it runs identically on both hosts with zero
 * per-platform code (closing a real bug: this module used to import a js_draw_card that only ever
 * drew a plain colour box, never the card's actual text). Same C source, two hosts.
 *
 * Audio does not need an import at all: sfx.c already has a real, existing offline-render API
 * (sfx_offline_begin/sfx_render/sfx_offline_end, used today by tests/test_sfx.c and
 * `dw_gui --fx-demo`'s own WAV dump) that never touches a real audio device -- this module puts
 * sfx.c into that mode once at init and exposes it as a pull: JS calls wasm_sfx_render(frames)
 * periodically and reads the rendered interleaved stereo PCM straight out of wasm memory, then
 * schedules it into Web Audio itself. sfx.c is built with -DDW_SFX_NO_SDL (see
 * scripts/build_wasm_fx.sh), which compiles its real SDL audio-device path out entirely (see
 * apps/gui/sfx.c's own #ifndef DW_SFX_NO_SDL guards around sfx_open/sfx_close) -- so this module
 * never even references real SDL audio types, and apps/wasm/fx/SDL.h never has to declare any. */
#include "fx.h"
#include "sfx.h"
#include "card_rules.h"
#include <stdint.h>

#define DW_HIDDEN_U8 255 /* core/protocol.h's own Merkle-Blindness hidden-energy sentinel */

extern void js_draw_text(int x, int y, int scale, int r, int g, int b, int a, const char *s);
extern int js_text_width(int scale, const char *s);

static void host_text(int x, int y, int scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char *s) {
    js_draw_text(x, y, scale, r, g, b, a, s);
}
static int host_text_w(int scale, const char *s) { return js_text_width(scale, s); }

int wasm_fx_unknown(void) { return FX_UNKNOWN; }

void wasm_fx_init(void) {
    static FxHost h;
    h.R = (SDL_Renderer *)0; /* opaque to fx.c; the browser host has exactly one implicit canvas context */
    h.text = host_text;
    h.text_w = host_text_w;
    fx_init(&h);
    sfx_offline_begin(); /* put sfx.c into pull-render mode for good -- see header comment above */
}

/* ================= FxRound builder (client sets fields, then wasm_fx_begin) ================= */
static FxRound g_r;
void fxr_reset(void) {
    FxRound z; z.round = 0;
    for (int s = 0; s < 2; s++) {
        z.card[s] = -1; z.eff[s] = -1; z.dmg[s] = 0; z.heal[s] = 0;
        z.hull_before[s] = 0; z.hull_after[s] = 0; z.armor_before[s] = 0; z.armor_after[s] = 0;
        z.vault_before[s] = 0; z.vault_after[s] = 0; z.energy_delta[s] = FX_UNKNOWN; z.energy_capped[s] = 0;
        z.flags[s] = 0; z.status_after[s] = 0; z.status_before[s] = 0;
    }
    z.lock_after = 0; z.lethal = 0; z.seed = 0;
    g_r = z;
}
void fxr_set_round(int v) { g_r.round = v; }
void fxr_set_card(int seat, int v) { g_r.card[seat] = v; }
void fxr_set_eff(int seat, int v) { g_r.eff[seat] = v; }
void fxr_set_dmg(int seat, int v) { g_r.dmg[seat] = v; }
void fxr_set_heal(int seat, int v) { g_r.heal[seat] = v; }
void fxr_set_hull_before(int seat, int v) { g_r.hull_before[seat] = v; }
void fxr_set_hull_after(int seat, int v) { g_r.hull_after[seat] = v; }
void fxr_set_armor_before(int seat, int v) { g_r.armor_before[seat] = v; }
void fxr_set_armor_after(int seat, int v) { g_r.armor_after[seat] = v; }
void fxr_set_vault_before(int seat, int v) { g_r.vault_before[seat] = v; }
void fxr_set_vault_after(int seat, int v) { g_r.vault_after[seat] = v; }
void fxr_set_energy_delta(int seat, int v) { g_r.energy_delta[seat] = v; }
void fxr_set_energy_capped(int seat, int v) { g_r.energy_capped[seat] = v; }
void fxr_set_flags(int seat, int v) { g_r.flags[seat] = v; }
void fxr_set_status_after(int seat, int v) { g_r.status_after[seat] = v; }
void fxr_set_status_before(int seat, int v) { g_r.status_before[seat] = v; }
void fxr_set_lock_after(int v) { g_r.lock_after = v; }
void fxr_set_lethal(int v) { g_r.lethal = v; }
void fxr_set_seed(unsigned int v) { g_r.seed = v; }
void wasm_fx_begin(void) { fx_begin(&g_r); }

/* ================= energy-delta estimate (apps/gui/main.c's own fx_energy_estimate, ported
 * verbatim -- not fx.c's own logic, but real UI-glue math this client needs the identical version
 * of to close the same honest gap web/README.md named: "the wire protocol has no direct energy
 * delta field", so the caller derives it from consecutive ROUND_START.energy values the same way
 * the native client already does, rather than a second, independently-derived approximation).
 * Operates on g_r's currently-set card[seat]/eff[seat] -- the caller must fxr_set_card/fxr_set_eff
 * for this round BEFORE calling this, then fxr_set_energy_delta/fxr_set_energy_capped with the
 * results, THEN wasm_fx_begin(). ================= */
static int has_channel(int id, int ch) { return id >= 0 && (fx_ch(card_fx_a(id)) == ch || fx_ch(card_fx_b(id)) == ch); }
int wasm_fx_energy_estimate(int seat, int rs_energy, int energy_next) {
    if (energy_next == DW_HIDDEN_U8 || rs_energy == DW_HIDDEN_U8) return FX_UNKNOWN;
    int card = g_r.card[seat];
    int base = card < 0 ? (rs_energy + 1 > 6 ? 6 : rs_energy + 1) : rs_energy - card_cost(card);
    int expect = base + 2 > 6 ? 6 : base + 2;
    return energy_next - expect;
}
int wasm_fx_energy_capped(int seat, int rs_energy, int energy_next) {
    if (energy_next == DW_HIDDEN_U8 || rs_energy == DW_HIDDEN_U8) return 0;
    int card = g_r.card[seat];
    int base = card < 0 ? (rs_energy + 1 > 6 ? 6 : rs_energy + 1) : rs_energy - card_cost(card);
    int expect = base + 2 > 6 ? 6 : base + 2;
    int probe = g_r.eff[seat] >= 0 ? g_r.eff[seat] : card;
    return (energy_next >= 6 && expect >= 6 && has_channel(probe, 23)) ? 1 : 0;
}

/* ================= timeline control / query ================= */
void wasm_fx_reset(void) { fx_reset(); }
void wasm_fx_set_speed(float sp) { fx_set_speed(sp); }
void wasm_fx_set_meters(float hull_you, float hull_opp, float armor_you, float armor_opp,
                         float energy_you, float energy_opp, float vault_you, float vault_opp, int snap) {
    float hull[2] = {hull_you, hull_opp}, armor[2] = {armor_you, armor_opp};
    float energy[2] = {energy_you, energy_opp}, vault[2] = {vault_you, vault_opp};
    fx_set_meters(hull, armor, energy, vault, snap);
}
void wasm_fx_update(unsigned dt_ms) { fx_update(dt_ms); }
int wasm_fx_active(void) { return fx_active(); }
int wasm_fx_elapsed_ms(void) { return fx_elapsed_ms(); }
void wasm_fx_finish(void) { fx_finish(); }
float wasm_fx_total_ms(void) { return fx_total_ms(); }
const char *wasm_fx_scenario_name(void) { return fx_scenario_name(); }
float wasm_fx_pulse(int seat, int meter) { return fx_pulse(seat, meter); }
void wasm_fx_set_redline(int hull_you, int hull_opp) { fx_set_redline(hull_you, hull_opp); }
void wasm_fx_match_end(int result) { fx_match_end(result); }
void wasm_fx_status_changed(int status_you, int status_opp, int lock_mask, int new_round) {
    fx_status_changed(status_you, status_opp, lock_mask, new_round);
}
int wasm_fx_shake_dx(void) { int dx, dy; fx_get_shake(&dx, &dy); return dx; }
int wasm_fx_shake_dy(void) { int dx, dy; fx_get_shake(&dx, &dy); return dy; }
static float shown_field(int meter, int seat) { FxShown s = fx_shown(); return ((float *)&s)[meter * 2 + seat]; }
float wasm_fx_shown_hull(int seat) { return shown_field(0, seat); }
float wasm_fx_shown_armor(int seat) { return shown_field(1, seat); }
float wasm_fx_shown_energy(int seat) { return shown_field(2, seat); }
float wasm_fx_shown_vault(int seat) { return shown_field(3, seat); }

/* ================= drawing ================= */
void wasm_fx_draw_arena(int reveal_you, int reveal_opp, int have_reveal, int reveal_round, int reveal_dmg_you, int reveal_dmg_opp) {
    fx_draw_arena(reveal_you, reveal_opp, have_reveal, reveal_round, reveal_dmg_you, reveal_dmg_opp);
}
void wasm_fx_draw_overlay(void) { fx_draw_overlay(); }
void wasm_fx_draw_status_panel(int seat, int x, int y, int w, int h) { fx_draw_status_panel(seat, x, y, w, h); }
void wasm_fx_draw_disabled_card(int x, int y, int w, int h) { fx_draw_disabled_card(x, y, w, h); }

/* ================= audio: pull-based render off sfx.c's real offline API ================= */
#define WASM_AUDIO_CHUNK_FRAMES 8192
static int16_t g_audio_buf[WASM_AUDIO_CHUNK_FRAMES * 2];
int16_t *wasm_audio_buf_ptr(void) { return g_audio_buf; }
int wasm_audio_chunk_capacity(void) { return WASM_AUDIO_CHUNK_FRAMES; }
int wasm_sfx_render(int frames) {
    if (frames > WASM_AUDIO_CHUNK_FRAMES) frames = WASM_AUDIO_CHUNK_FRAMES;
    if (frames < 0) frames = 0;
    return sfx_render(g_audio_buf, frames);
}
void wasm_sfx_set_muted(int m) { sfx_set_muted(m); }
int wasm_sfx_is_muted(void) { return sfx_is_muted(); }
int wasm_sfx_active_voices(void) { return sfx_active_voices(); }
void wasm_sfx_stop_all(void) { sfx_stop_all(); }
