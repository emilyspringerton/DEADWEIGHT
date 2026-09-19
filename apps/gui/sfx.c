#include "sfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef DW_SFX_NO_SDL
#include <SDL.h>
#endif

typedef enum { W_SINE, W_SQUARE, W_SAW, W_TRI, W_NOISE } Wave;

typedef struct {
    int active, prio;
    long delay;                  /* samples until the voice starts */
    long age, len;               /* samples played / total */
    Wave wave;
    double ph, f, fmul;          /* phase [0,1), current frequency, per-sample glide ratio */
    float a, d, s, r;            /* seconds, s = sustain level */
    float lp0, lp1, lp;          /* low-pass cutoff start/end (Hz; 0 = off), running one-pole state */
    float lpk; float lpy[2];
    float pan0, pan1;
    float gain, drive;
    uint32_t rng;
} Voice;

static Voice V[SFX_MAX_VOICES];
static int muted = 0, offline = 0, open_dev = 0;
static uint32_t seed_ctr = 0x9E3779B9u;
static float cur_master = 1.0f;   /* per-cue loudness trim (the synth patches are voiced flat; some timbres just read quieter) */
#ifndef DW_SFX_NO_SDL
static SDL_AudioDeviceID dev = 0;
#endif

static float mtof(float note) { return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f); }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* ---- voice allocation ---- */
static void add_voice(Wave w, float f0, float f1, float dur, float a, float d, float s, float r, float lp0, float lp1,
                      float pan0, float pan1, float gain, int prio, float drive, float delay_ms, const SfxOpts *o) {
    int slot = -1, lowest = -1; int lowp = 99;
    for (int i = 0; i < SFX_MAX_VOICES; i++) {
        if (!V[i].active) { slot = i; break; }
        if (V[i].prio < lowp || (V[i].prio == lowp && lowest >= 0 && V[i].age > V[lowest].age)) { lowp = V[i].prio; lowest = i; }
    }
    if (slot < 0) { if (lowp >= prio) return; slot = lowest; }   /* full: only steal from strictly lower priority */
    Voice *v = &V[slot]; memset(v, 0, sizeof *v);
    float pitch = powf(2.0f, o->pitch_st / 12.0f);
    v->active = 1; v->prio = prio; v->wave = w;
    v->delay = (long)((delay_ms + o->delay_ms) * SFX_RATE / 1000.0f);
    v->len = (long)(dur * SFX_RATE);
    v->f = f0 * pitch; v->fmul = (f1 > 0 && f0 > 0 && dur > 0) ? (double)powf(f1 / f0, 1.0f / (dur * SFX_RATE)) : 1.0;
    v->a = a; v->d = d; v->s = s; v->r = r;
    v->lp0 = lp0; v->lp1 = lp1 > 0 ? lp1 : lp0;
    if (o->muffle) { if (v->lp0 == 0 || v->lp0 > 650) v->lp0 = 650; if (v->lp1 == 0 || v->lp1 > 650) v->lp1 = 650; }
    v->pan0 = clampf(pan0 + o->pan, -1, 1); v->pan1 = clampf(pan1 + o->pan, -1, 1);
    v->gain = gain * cur_master * (o->intensity > 0 ? o->intensity : 1.0f) * (o->muffle ? 0.8f : 1.0f); v->drive = drive;
    seed_ctr = seed_ctr * 1664525u + 1013904223u; v->rng = seed_ctr | 1u;
}

/* shorthand used by the cue definitions below; `dl` is the delay inside the cue (ms) */
#define VC(w, f0, f1, dur, a, d, s, r, lp0, lp1, pn0, pn1, g, pr, drv, dl) add_voice(w, f0, f1, dur, a, d, s, r, lp0, lp1, pn0, pn1, g, pr, drv, dl, &o)
#define NOTE(w, n, dur, g, pr, dl, lp) VC(w, mtof(n), mtof(n), dur, 0.004f, dur * 0.7f, 0.0f, 0.03f, lp, lp, 0, 0, g, pr, 0, dl)

static void arp(SfxOpts o, Wave w, const float *notes, int n, float spacing_ms, float g, int prio, float lastdur) {
    for (int i = 0; i < n; i++) NOTE(w, notes[i], i == n - 1 ? lastdur : 0.09f, g * (i == n - 1 ? 1.15f : 1.0f), prio, i * spacing_ms, i == n - 1 ? 9000 : 5500);
}

static void chord(SfxOpts o, Wave w, const float *notes, int n, float dur, float a, float g, int prio, float dl, float glide_st) {
    for (int i = 0; i < n; i++) {
        float f = mtof(notes[i]);
        VC(w, f, f * powf(2.0f, glide_st / 12.0f), dur, a, dur * 0.4f, 0.55f, dur * 0.35f, 4200, 4200, -0.15f + 0.1f * i, -0.15f + 0.1f * i, g, prio, 0, dl);
    }
}

static void noise_hit(SfxOpts o, float dur, float lp0, float lp1, float g, int prio, float dl, float a) {
    VC(W_NOISE, 0, 0, dur, a, dur * 0.8f, 0.0f, 0.02f, lp0, lp1, 0, 0, g, prio, 0, dl);
}

/* ---- the cues ---- */
static void cue(SfxCue c, SfxOpts o) {
    const int CL = SFX_PRIO_CLASH, HU = SFX_PRIO_HULL, AR = SFX_PRIO_ARMOR, EC = SFX_PRIO_ECON, AM = SFX_PRIO_AMBIENCE;
    float I = o.intensity > 0 ? o.intensity : 1.0f;
    switch (c) {
    case SFX_FLIP:
        noise_hit(o, 0.05f, 6000, 1500, 0.25f, AM, 0, 0.001f); VC(W_SINE, 500, 260, 0.06f, 0.001f, 0.05f, 0, 0.01f, 0, 0, 0, 0, 0.25f, AM, 0, 40);
        break;
    /* -------- Red: heavy percussion + noise -------- */
    case SFX_BLITZ:
        VC(W_SINE, 150, 42, 0.24f, 0.001f, 0.22f, 0, 0.02f, 0, 0, 0, 0, 1.0f, CL, 2.2f, 0);
        noise_hit(o, 0.30f, 6000, 350, 0.55f, CL, 0, 0.001f);
        VC(W_SQUARE, 330, 300, 0.16f, 0.001f, 0.15f, 0, 0.02f, 2600, 800, 0, 0, 0.22f, CL, 0, 0);
        VC(W_SINE, 118, 52, 0.16f, 0.001f, 0.15f, 0, 0.02f, 0, 0, 0, 0, 0.7f, CL, 1.5f, 95);
        noise_hit(o, 0.14f, 4500, 500, 0.32f, CL, 95, 0.001f);
        break;
    case SFX_BLITZ_CRIT:
        cue(SFX_BLITZ, o);
        VC(W_SINE, 52, 30, 0.75f, 0.002f, 0.7f, 0, 0.05f, 0, 0, 0, 0, 1.15f, CL, 3.0f, 0);      /* sub-bass kick */
        VC(W_SAW, 72, 38, 0.55f, 0.001f, 0.5f, 0, 0.05f, 900, 200, 0, 0, 0.32f, CL, 6.0f, 0);   /* distorted growl */
        noise_hit(o, 0.40f, 9000, 1800, 0.30f, CL, 110, 0.001f);                                  /* debris on the glass */
        break;
    /* -------- Blue: pads + crystalline chimes -------- */
    case SFX_BLOCK: {
        static const float pad[3] = {60, 64, 67};
        chord(o, W_TRI, pad, 3, 0.95f, 0.28f, 0.26f, CL, 0, 0.0f);
        VC(W_SINE, 440, 880, 0.7f, 0.35f, 0.2f, 0.5f, 0.15f, 0, 0, 0, 0, 0.18f, CL, 0, 0);
        noise_hit(o, 0.5f, 1500, 9000, 0.28f, CL, 0, 0.36f);                                       /* reverse-cymbal swell into the absorb */
        VC(W_SINE, 200, 95, 0.16f, 0.002f, 0.15f, 0, 0.02f, 0, 0, 0, 0, 0.55f, CL, 0, 340);
        VC(W_SINE, mtof(96), mtof(96), 0.38f, 0.002f, 0.36f, 0, 0.03f, 0, 0, -0.3f, -0.3f, 0.17f, CL, 0, 400);
        VC(W_SINE, mtof(100), mtof(100), 0.38f, 0.002f, 0.36f, 0, 0.03f, 0, 0, 0.2f, 0.2f, 0.15f, CL, 0, 450);
        VC(W_SINE, mtof(103), mtof(103), 0.38f, 0.002f, 0.36f, 0, 0.03f, 0, 0, 0.0f, 0.0f, 0.13f, CL, 0, 500);
        break;
    }
    case SFX_BLOCK_CRIT: {
        static const float lo[3] = {60, 64, 67}, hi[4] = {72, 76, 79, 84};
        chord(o, W_TRI, lo, 3, 0.9f, 0.1f, 0.24f, CL, 0, 12.0f);                                   /* sweep up an octave */
        chord(o, W_TRI, hi, 4, 1.1f, 0.02f, 0.24f, CL, 620, 0.0f);                                 /* triumphant resolving major chord */
        noise_hit(o, 0.45f, 2000, 9500, 0.26f, CL, 0, 0.3f);
        VC(W_SINE, 180, 90, 0.18f, 0.002f, 0.16f, 0, 0.02f, 0, 0, 0, 0, 0.6f, CL, 0, 300);
        for (int i = 0; i < 5; i++) NOTE(W_SINE, 96 + i * 3, 0.3f, 0.13f, CL, 660 + i * 55, 0);
        break;
    }
    /* -------- Yellow: precise arpeggios and data blips (one signature per keyword) -------- */
    case SFX_BYPASS_LOCK: {
        for (int i = 0; i < 6; i++) VC(W_SQUARE, 1800, 1800, 0.035f, 0.001f, 0.03f, 0, 0.005f, 6000, 6000, 0, 0, 0.16f, CL, 0, i * (95 - i * 10));   /* accelerating targeting beeps */
        VC(W_SQUARE, 2300, 2300, 0.09f, 0.001f, 0.08f, 0, 0.01f, 8000, 8000, 0, 0, 0.22f, CL, 0, 330);                                          /* lock tone */
        noise_hit(o, 0.05f, 700, 300, 0.4f, CL, 330, 0.001f); VC(W_SINE, 150, 90, 0.10f, 0.001f, 0.09f, 0, 0.01f, 0, 0, 0, 0, 0.5f, CL, 0, 330);   /* clunk */
        static const float fin[3] = {84, 88, 91}; arp(o, W_SQUARE, fin, 3, 60, 0.17f, CL, 0.28f);
        break;
    }
    case SFX_BYPASS_SABOTAGE: {
        static const float down[5] = {91, 86, 82, 77, 72};
        for (int i = 0; i < 5; i++) NOTE(W_SQUARE, down[i], 0.06f, 0.16f, CL, i * 55, 7000);           /* worm in */
        for (int i = 0; i < 7; i++) noise_hit(o, 0.03f, 8000, 3000, 0.25f, CL, 280 + i * 47 + (i * 37) % 23, 0.001f);   /* short-circuit crackle */
        VC(W_SAW, 320, 60, 0.32f, 0.002f, 0.3f, 0, 0.03f, 2500, 300, 0, 0, 0.3f, CL, 2.0f, 560);        /* the shield collapsing from within */
        static const float fin[3] = {84, 88, 91}; arp(o, W_SQUARE, fin, 3, 70, 0.15f, CL, 0.25f); (void)fin;
        break;
    }
    case SFX_BYPASS_FLANK:
        noise_hit(o, 0.45f, 700, 7500, 0.32f, CL, 0, 0.22f);                                            /* thruster whoosh */
        VC(W_SQUARE, 900, 900, 0.07f, 0.001f, 0.06f, 0, 0.01f, 6000, 6000, -0.4f, -0.4f, 0.2f, CL, 0, 300);
        VC(W_SQUARE, 1400, 1400, 0.07f, 0.001f, 0.06f, 0, 0.01f, 8000, 8000, 0.4f, 0.4f, 0.2f, CL, 0, 360);
        { static const float fin[4] = {79, 84, 88, 91}; arp(o, W_SQUARE, fin, 4, 55, 0.16f, CL, 0.3f); }
        break;
    case SFX_BYPASS_SCAN:
        VC(W_SINE, 300, 1800, 0.42f, 0.02f, 0.3f, 0.3f, 0.1f, 0, 0, 0, 0, 0.2f, CL, 0, 0);              /* sensor wave */
        VC(W_SINE, 1400, 1360, 0.7f, 0.002f, 0.7f, 0, 0.05f, 0, 0, 0, 0, 0.45f, CL, 0, 380);           /* sonar ping + two echoes */
        VC(W_SINE, 1400, 1360, 0.5f, 0.002f, 0.5f, 0, 0.05f, 0, 0, -0.3f, -0.3f, 0.2f, CL, 0, 560);
        VC(W_SINE, 1400, 1360, 0.4f, 0.002f, 0.4f, 0, 0.05f, 0, 0, 0.3f, 0.3f, 0.1f, CL, 0, 740);
        { static const float fin[3] = {88, 91, 95}; arp(o, W_SQUARE, fin, 3, 60, 0.14f, CL, 0.28f); }
        break;
    case SFX_BYPASS_SIPHON:
        for (int i = 0; i < 12; i++) VC(W_SINE, 900 + (i % 3) * 220, 900 + (i % 3) * 220, 0.05f, 0.002f, 0.04f, 0, 0.01f, 0, 0, -1, 1, 0.13f, CL, 0, i * 38);   /* a stream that pans loser -> winner */
        VC(W_NOISE, 0, 0, 0.5f, 0.05f, 0.4f, 0.2f, 0.1f, 2500, 4500, -1, 1, 0.07f, CL, 0, 0);
        { static const float fin[3] = {84, 88, 91}; arp(o, W_SQUARE, fin, 3, 60, 0.14f, CL, 0.28f); }
        break;
    case SFX_BYPASS_CRIT: {
        VC(W_SAW, 200, 1800, 0.35f, 0.002f, 0.33f, 0, 0.02f, 6000, 6000, 0, 0, 0.3f, CL, 3.0f, 0);      /* polarity reversal */
        for (int i = 0; i < 6; i++) NOTE(W_SQUARE, 96 - i * 4, 0.05f, 0.16f, CL, 350 + i * 40, 7000);
        VC(W_SINE, 92, 48, 0.55f, 0.002f, 0.5f, 0, 0.05f, 0, 0, 0, 0, 0.95f, CL, 3.0f, 580);            /* crushed by its own shield */
        noise_hit(o, 0.3f, 5000, 400, 0.4f, CL, 580, 0.001f);
        break;
    }
    case SFX_MIRROR_OFFENSE:
        VC(W_SINE, 140, 45, 0.22f, 0.001f, 0.2f, 0, 0.02f, 0, 0, -0.5f, -0.5f, 0.9f, CL, 2.0f, 0);
        VC(W_SINE, 140, 45, 0.22f, 0.001f, 0.2f, 0, 0.02f, 0, 0, 0.5f, 0.5f, 0.9f, CL, 2.0f, 0);
        noise_hit(o, 0.35f, 7000, 500, 0.5f, CL, 20, 0.001f);
        break;
    case SFX_MIRROR_OPERATIONS:
        noise_hit(o, 0.3f, 9000, 1500, 0.35f, CL, 0, 0.001f);
        VC(W_SQUARE, mtof(84), mtof(84), 0.3f, 0.002f, 0.28f, 0, 0.02f, 6000, 6000, -0.4f, -0.4f, 0.17f, CL, 0, 0);
        VC(W_SQUARE, mtof(90), mtof(90), 0.3f, 0.002f, 0.28f, 0, 0.02f, 6000, 6000, 0.4f, 0.4f, 0.17f, CL, 0, 0);        /* tritone: two systems jamming each other */
        break;
    case SFX_MIRROR_DEFENSE:
        NOTE(W_SINE, 88, 0.4f, 0.22f, CL, 0, 0); NOTE(W_SINE, 91, 0.4f, 0.22f, CL, 120, 0);
        VC(W_SINE, 160, 80, 0.2f, 0.002f, 0.18f, 0, 0.02f, 0, 0, 0, 0, 0.5f, CL, 0, 0);
        break;
    case SFX_UNOPPOSED:
        VC(W_SINE, 140, 50, 0.2f, 0.001f, 0.18f, 0, 0.02f, 0, 0, 0, 0, 0.8f, CL, 1.6f, 0); noise_hit(o, 0.22f, 5000, 500, 0.4f, CL, 0, 0.001f);
        break;
    case SFX_HOLD: NOTE(W_TRI, 55, 0.5f, 0.12f, AM, 0, 1500); break;
    case SFX_CANCELLED:
        for (int i = 0; i < 6; i++) VC(W_SQUARE, 900 - i * 120, 700 - i * 110, 0.04f, 0.001f, 0.03f, 0, 0.005f, 3000, 3000, 0, 0, 0.16f, CL, 0, i * 45);
        VC(W_SAW, 260, 50, 0.3f, 0.002f, 0.28f, 0, 0.02f, 1500, 200, 0, 0, 0.25f, CL, 2.0f, 260);
        break;
    case SFX_IMMUNE: { static const float ch[4] = {84, 88, 91, 96}; for (int i = 0; i < 4; i++) NOTE(W_SINE, ch[i], 0.5f, 0.16f, CL, i * 30, 0); break; }
    case SFX_LIFELINE:
        VC(W_SINE, 62, 44, 0.12f, 0.002f, 0.11f, 0, 0.01f, 0, 0, 0, 0, 0.8f, CL, 0, 0); VC(W_SINE, 62, 44, 0.12f, 0.002f, 0.11f, 0, 0.01f, 0, 0, 0, 0, 0.6f, CL, 0, 170);
        VC(W_TRI, mtof(60), mtof(84), 0.6f, 0.1f, 0.3f, 0.5f, 0.2f, 0, 0, 0, 0, 0.22f, CL, 0, 300);
        break;
    case SFX_SWAP:
        noise_hit(o, 0.3f, 900, 6000, 0.25f, CL, 0, 0.12f); VC(W_NOISE, 0, 0, 0.3f, 0.12f, 0.2f, 0, 0.05f, 6000, 900, 1, -1, 0.25f, CL, 0, 0);
        NOTE(W_SQUARE, 84, 0.06f, 0.16f, CL, 120, 6000); NOTE(W_SQUARE, 88, 0.06f, 0.16f, CL, 190, 6000);
        break;
    /* -------- resources: short, distinct timbres, <= 0.6 s -------- */
    case SFX_HULL_HIT:
        noise_hit(o, 0.22f, 1400, 180, 0.6f * I, HU, 0, 0.001f);
        VC(W_SINE, 95, 40, 0.2f, 0.001f, 0.19f, 0, 0.01f, 0, 0, 0, 0, 0.8f * I, HU, 3.0f, 0);
        break;
    case SFX_HEAL:
        VC(W_SINE, 660, 990, 0.28f, 0.01f, 0.2f, 0.3f, 0.1f, 0, 0, 0, 0, 0.26f, AR, 0, 0); NOTE(W_SINE, 88, 0.3f, 0.12f, AR, 90, 0);
        break;
    case SFX_ARMOR_GAIN:
        VC(W_SINE, 2400, 2700, 0.22f, 0.001f, 0.2f, 0, 0.02f, 0, 0, 0, 0, 0.22f, AR, 0, 0);            /* shing */
        VC(W_SQUARE, 1200, 1350, 0.1f, 0.001f, 0.09f, 0, 0.01f, 5000, 3000, 0, 0, 0.09f, AR, 0, 0);
        noise_hit(o, 0.03f, 3500, 1200, 0.4f, AR, 95, 0.001f); VC(W_SINE, 620, 480, 0.08f, 0.001f, 0.07f, 0, 0.01f, 0, 0, 0, 0, 0.4f, AR, 0, 95);   /* ...and lock */
        break;
    case SFX_ARMOR_LOSS: {
        static const float d[6] = {88, 84, 79, 74, 68, 62};
        for (int i = 0; i < 6; i++) NOTE(W_SQUARE, d[i], 0.08f, 0.16f, AR, i * 55, 4200 - i * 500);        /* melting down */
        break;
    }
    case SFX_ENERGY_GAIN:
        VC(W_SINE, 440, 880, 0.2f, 0.005f, 0.15f, 0.2f, 0.05f, 0, 0, 0, 0, 0.3f, EC, 0, 0); VC(W_SQUARE, 880, 880, 0.06f, 0.002f, 0.05f, 0, 0.01f, 5000, 5000, 0, 0, 0.1f, EC, 0, 110);
        break;
    case SFX_CREDITS_GAIN:
        VC(W_SINE, 1318, 1318, 0.12f, 0.001f, 0.1f, 0, 0.02f, 0, 0, 0, 0, 0.26f, EC, 0, 0);            /* cha */
        VC(W_SINE, 1760, 1760, 0.4f, 0.001f, 0.38f, 0, 0.03f, 0, 0, 0, 0, 0.28f, EC, 0, 70);          /* -ching */
        VC(W_SINE, 3520, 3520, 0.25f, 0.001f, 0.24f, 0, 0.02f, 0, 0, 0, 0, 0.08f, EC, 0, 70);
        break;
    case SFX_RESOURCE_LOSS:
        VC(W_SINE, 320, 200, 0.16f, 0.004f, 0.14f, 0, 0.02f, 900, 500, 0, 0, 0.22f, EC, 0, 0);
        break;
    case SFX_SIPHON_STREAM:
        for (int i = 0; i < 10; i++) VC(W_SINE, 1000 + (i & 1) * 260, 1000 + (i & 1) * 260, 0.04f, 0.002f, 0.035f, 0, 0.005f, 0, 0, -1, 1, 0.09f, EC, 0, i * 32);
        VC(W_NOISE, 0, 0, 0.36f, 0.04f, 0.3f, 0.2f, 0.06f, 2600, 4200, -1, 1, 0.06f, EC, 0, 0);
        break;
    case SFX_OVERFLOW:
        noise_hit(o, 0.28f, 9000, 3000, 0.16f, EC, 0, 0.001f);
        for (int i = 0; i < 4; i++) NOTE(W_SQUARE, 96 - i * 5, 0.04f, 0.09f, EC, i * 45, 6000);
        break;
    /* -------- statuses and ambience -------- */
    case SFX_BURN_START:
        VC(W_NOISE, 0, 0, 0.5f, 0.06f, 0.3f, 0.2f, 0.15f, 500, 3200, 0, 0, 0.32f, HU, 0, 0);
        for (int i = 0; i < 5; i++) noise_hit(o, 0.02f, 7000, 2500, 0.2f, HU, 120 + i * 63, 0.001f);
        break;
    case SFX_BURN_TICK:   /* a slow filter sweep back and forth (fire), plus pops */
        VC(W_NOISE, 0, 0, 0.55f, 0.08f, 0.3f, 0.5f, 0.15f, 350, 2200, -0.15f, -0.15f, 0.13f, AM, 0, 0);
        VC(W_NOISE, 0, 0, 0.55f, 0.08f, 0.3f, 0.5f, 0.15f, 2200, 350, 0.15f, 0.15f, 0.13f, AM, 0, 550);
        for (int i = 0; i < 4; i++) noise_hit(o, 0.015f, 6500, 2000, 0.16f, AM, 90 + i * 260 + (i * 71) % 40, 0.001f);
        break;
    case SFX_EMP:
        VC(W_SAW, 900, 40, 0.75f, 0.001f, 0.7f, 0, 0.05f, 4000, 250, 0, 0, 0.42f, CL, 1.4f, 0);        /* power-down whine */
        noise_hit(o, 0.06f, 3000, 800, 0.3f, CL, 0, 0.001f);
        break;
    case SFX_HEARTBEAT:
        VC(W_SINE, 62, 44, 0.11f, 0.002f, 0.1f, 0, 0.01f, 0, 0, 0, 0, 0.7f, AM, 0, 0); VC(W_SINE, 58, 42, 0.11f, 0.002f, 0.1f, 0, 0.01f, 0, 0, 0, 0, 0.5f, AM, 0, 170);
        break;
    case SFX_REDLINE_ALARM:
        NOTE(W_TRI, 57, 0.18f, 0.18f, AM, 0, 1800); NOTE(W_TRI, 57, 0.18f, 0.18f, AM, 260, 1800);
        break;
    case SFX_UI_LOCK: noise_hit(o, 0.03f, 4000, 1500, 0.25f, AM, 0, 0.001f); NOTE(W_SQUARE, 76, 0.05f, 0.1f, AM, 0, 5000); break;
    case SFX_UI_TICK: NOTE(W_SINE, 90, 0.03f, 0.08f, AM, 0, 0); break;
    default: break;
    }
}

/* ---- mixer ---- */
static uint32_t xr(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void mix_frames(float *out, int frames) {   /* out: interleaved stereo float */
    for (int n = 0; n < frames; n++) {
        float L = 0, R = 0;
        for (int i = 0; i < SFX_MAX_VOICES; i++) {
            Voice *v = &V[i];
            if (!v->active) continue;
            if (v->delay > 0) { v->delay--; continue; }
            if (v->age >= v->len) { v->active = 0; continue; }
            float t = (float)v->age / SFX_RATE, dur = (float)v->len / SFX_RATE, e;
            if (t < v->a) e = v->a > 0 ? t / v->a : 1;
            else if (t < v->a + v->d) e = 1.0f - (1.0f - v->s) * ((t - v->a) / (v->d > 0 ? v->d : 1));
            else e = v->s;
            if (v->r > 0 && t > dur - v->r) e *= (dur - t) / v->r;
            float x;
            switch (v->wave) {
            case W_SINE: x = sinf(6.2831853f * (float)v->ph); break;
            case W_SQUARE: x = v->ph < 0.5 ? 1.0f : -1.0f; x *= 0.6f; break;
            case W_SAW: x = 2.0f * (float)v->ph - 1.0f; x *= 0.7f; break;
            case W_TRI: x = 4.0f * fabsf((float)v->ph - 0.5f) - 1.0f; break;
            default: x = ((float)(xr(&v->rng) & 0xFFFF) / 32768.0f) - 1.0f; break;
            }
            v->ph += v->f / SFX_RATE; if (v->ph >= 1.0) v->ph -= 1.0; v->f *= v->fmul;
            if (v->lp0 > 0) {
                float k_t = dur > 0 ? t / dur : 0, fc = v->lp0 + (v->lp1 - v->lp0) * k_t;
                float k = 1.0f - expf(-6.2831853f * fc / SFX_RATE);
                v->lpy[0] += k * (x - v->lpy[0]); v->lpy[1] += k * (v->lpy[0] - v->lpy[1]); x = v->lpy[1];
            }
            x *= e * v->gain;
            if (v->drive > 0) x = tanhf(x * (1.0f + v->drive)) / (1.0f + v->drive * 0.3f);
            float pan = v->pan0 + (v->pan1 - v->pan0) * (dur > 0 ? t / dur : 0);
            float ang = (pan + 1.0f) * 0.78539816f;                     /* equal-power pan */
            L += x * cosf(ang); R += x * sinf(ang);
            v->age++;
        }
        out[2 * n] = tanhf(L * 0.55f); out[2 * n + 1] = tanhf(R * 0.55f);   /* soft master limiter */
    }
}

int sfx_render(int16_t *out, int frames) {
    static float tmp[4096 * 2];
    int done = 0;
    while (done < frames) {
        int n = frames - done > 4096 ? 4096 : frames - done;
        mix_frames(tmp, n);
        for (int i = 0; i < n * 2; i++) out[done * 2 + i] = (int16_t)(clampf(tmp[i], -1, 1) * 32000.0f);
        done += n;
    }
    return frames;
}

void sfx_play(SfxCue c, SfxOpts o) {
    if (muted || (!open_dev && !offline) || c < 0 || c >= SFX_COUNT) return;
#ifndef DW_SFX_NO_SDL
    if (dev) SDL_LockAudioDevice(dev);
#endif
    switch (c) {   /* loudness trims: the clash stingers must dominate, and the thin square/sine timbres need help to read at equal level */
    case SFX_BLOCK: case SFX_BLOCK_CRIT: cur_master = 1.5f; break;
    case SFX_BYPASS_LOCK: case SFX_BYPASS_SABOTAGE: case SFX_BYPASS_FLANK: case SFX_BYPASS_SCAN: case SFX_BYPASS_SIPHON: case SFX_BYPASS_CRIT: cur_master = 2.4f; break;
    case SFX_MIRROR_OPERATIONS: case SFX_MIRROR_DEFENSE: cur_master = 2.0f; break;
    case SFX_ARMOR_LOSS: cur_master = 2.2f; break;
    case SFX_SIPHON_STREAM: case SFX_OVERFLOW: case SFX_BURN_START: case SFX_RESOURCE_LOSS: cur_master = 1.6f; break;
    default: cur_master = 1.0f; break;
    }
    cue(c, o);
    cur_master = 1.0f;
#ifndef DW_SFX_NO_SDL
    if (dev) SDL_UnlockAudioDevice(dev);
#endif
}
void sfx_play_simple(SfxCue c) { SfxOpts o = {0, 0, 0, 1.0f, 0}; sfx_play(c, o); }
void sfx_stop_all(void) {
#ifndef DW_SFX_NO_SDL
    if (dev) SDL_LockAudioDevice(dev);
#endif
    memset(V, 0, sizeof V);
#ifndef DW_SFX_NO_SDL
    if (dev) SDL_UnlockAudioDevice(dev);
#endif
}
int sfx_active_voices(void) { int n = 0; for (int i = 0; i < SFX_MAX_VOICES; i++) n += V[i].active; return n; }
void sfx_set_muted(int m) { muted = m; if (m) sfx_stop_all(); }
int sfx_is_muted(void) { return muted; }
int sfx_is_open(void) { return open_dev; }

void sfx_offline_begin(void) { memset(V, 0, sizeof V); offline = 1; }
void sfx_offline_end(void) { memset(V, 0, sizeof V); offline = 0; }

#ifndef DW_SFX_NO_SDL
static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    int frames = len / (int)(2 * sizeof(int16_t));
    sfx_render((int16_t *)stream, frames);
}
int sfx_open(void) {
    if (open_dev) return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 1;
    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq = SFX_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev || have.freq != SFX_RATE || have.format != AUDIO_S16SYS || have.channels != 2) { if (dev) SDL_CloseAudioDevice(dev); dev = 0; return 2; }
    open_dev = 1; SDL_PauseAudioDevice(dev, 0);
    return 0;
}
void sfx_close(void) { if (dev) { SDL_CloseAudioDevice(dev); dev = 0; } open_dev = 0; }
#else
int sfx_open(void) { return 1; }
void sfx_close(void) {}
#endif

int sfx_write_wav(const char *path, const int16_t *st, int frames) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    uint32_t data = (uint32_t)frames * 4, riff = 36 + data, rate = SFX_RATE, brate = SFX_RATE * 4, fmt = 16; uint16_t pcm = 1, ch = 2, ba = 4, bps = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(st, 4, (size_t)frames, f); fclose(f); return 0;
}

void sfx_stats(const int16_t *st, int frames, float thresh, float *peak, float *rms, int *last_loud) {
    double sum = 0; float pk = 0; int last = -1;
    for (int i = 0; i < frames; i++) {
        float l = st[2 * i] / 32768.0f, r = st[2 * i + 1] / 32768.0f, m = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
        if (m > pk) pk = m;
        if (m > thresh) last = i;
        sum += l * l + r * r;
    }
    if (peak) *peak = pk;
    if (rms) *rms = frames ? (float)sqrt(sum / (2.0 * frames)) : 0;
    if (last_loud) *last_loud = last;
}
