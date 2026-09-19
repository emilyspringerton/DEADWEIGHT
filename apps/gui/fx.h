/* fx -- DEADWEIGHT's round-resolution animation engine for the SDL2 client.
 *
 * A finished round becomes one timeline (<= 9 s, usually 4-6 s) that plays in the arena band of the match screen while the
 * hand stays playable:
 *   0.0-0.6 s  both cards flip together (scale punch + a little screen shake); two ship silhouettes appear facing each other
 *   clash      Offense beats Operations  = THE BLITZ    (missiles shatter the Operations UI)
 *              Defense beats Offense     = THE BLOCK    (a hex shield absorbs, sometimes reflects)
 *              Operations beats Defense  = THE BYPASS   (one signature per keyword: Lock, Sabotage, Flank, Scan, Siphon)
 *              + Critical variants when the winner wins big: Overwhelm, Perfect Deflection, Deep System Hijack
 *              + mirror clashes, unopposed hits, cancelled / immune / lifeline / copied / swapped overlays
 *   resources  strictly after the clash, never overlapping it: Hull -> Armor -> Energy/Credits -> status changes
 * plus persistent state: burn embers, regen sparkles, EMP-disabled cards, the hull "redline" alarm, overloaded meters,
 * and combo escalation (each chained Operations effect moves 20% faster).
 * Every visual beat has a matching sfx cue scheduled on the same clock (see sfx.h). The engine draws with plain SDL2
 * rectangles/lines only (no textures), so it runs unchanged on the software renderer used by the headless selftest. */
#ifndef DW_FX_H
#define DW_FX_H
#include <SDL.h>
#include <stdint.h>

typedef struct {
    SDL_Renderer *R;
    void (*text)(int x, int y, int scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char *s);
    int  (*text_w)(int scale, const char *s);
    /* draw one card (host's card_box): state 0 normal, 1 selected, 2 disabled, 3 locked */
    void (*card)(int x, int y, int w, int h, int id, int state);
} FxHost;

typedef struct {
    int round;
    int card[2], eff[2];               /* seat 0 = you, 1 = opponent; declared card and the card that actually resolved (-1 = pass/cancelled) */
    int dmg[2], heal[2];               /* hull lost / healed by each seat this round */
    int hull_before[2], hull_after[2];
    int armor_before[2], armor_after[2];
    int vault_before[2], vault_after[2];   /* credits; -128 = hidden from us */
    int energy_delta[2];               /* best-effort net effect on energy, or FX_UNKNOWN */
    int energy_capped[2];              /* the gain hit the cap (overload) */
    int flags[2];                      /* DW_FX_* per seat (cancelled, immune, lifeline, locked opp, swapped, discard, redraw, copied) */
    int status_after[2];               /* burn / regen / hidden bits after this round */
    int status_before[2];
    int lock_after;                    /* my hand slots disabled for the next round (bitmask) */
    int lethal;                        /* the match ended this round */
    uint32_t seed;
} FxRound;
#define FX_UNKNOWN (-1000)

typedef struct { float hull[2], armor[2], energy[2], vault[2]; } FxShown;

void fx_init(const FxHost *host);
void fx_reset(void);                                   /* new match / back to menu: clear timeline, particles, statuses */
void fx_set_speed(float speed);                        /* 1 = real time; >1 fast-forwards (headless selftest) */
void fx_set_meters(const float hull[2], const float armor[2], const float energy[2], const float vault[2], int snap);
void fx_begin(const FxRound *r);                       /* start resolution animation + schedule its audio */
void fx_update(unsigned dt_ms);
int  fx_active(void);
int  fx_elapsed_ms(void);
void fx_finish(void);                                  /* jump to the end (settles meters) */
void fx_draw_arena(int reveal_you, int reveal_opp, int have_reveal);   /* the arena band y=170..420; falls back to the static reveal when idle */
void fx_draw_overlay(void);                            /* redline frame, vignettes; call last */
void fx_draw_status_panel(int seat, int x, int y, int w, int h);       /* embers / regen sparkles over a hull panel */
void fx_draw_disabled_card(int x, int y, int w, int h);                /* EMP look for a disabled hand slot */
void fx_get_shake(int *dx, int *dy);
FxShown fx_shown(void);                                /* meter values to DRAW (they lag the server until the resource stage) */
float fx_pulse(int seat, int meter);                   /* 0..1 scale-pulse strength for a meter (0 hull, 1 armor, 2 energy, 3 credits) */
void fx_set_redline(int hull_you, int hull_opp);       /* <=20% hull turns on the alarm frame + heartbeat */
void fx_status_changed(int status_you, int status_opp, int lock_mask_you, int new_round);   /* ROUND_START: burn tick audio, EMP stinger */
const char *fx_scenario_name(void);                    /* for logs/tests: "blitz", "block_crit", "bypass_scan", ... */
float fx_total_ms(void);                               /* length of the current/last timeline in ms (already speed-scaled = real ms) */
#endif
