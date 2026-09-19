#include "fx.h"
#include "sfx.h"
#include "card_rules.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------------------------------------------- layout */
#define AY 170                       /* arena band */
#define AH 250
#define SHIP_Y 296
#define SHIELD_R 48.0f
static const float SHIP_X[2] = {174, 306};
static const int   SHIP_DIR[2] = {1, -1};
static const int   CARD_SX[2] = {6, 382}, CARD_SY = 236, CARD_SW = 92, CARD_SH = 150;      /* small cards during the clash */
static const int   BIG_X[2] = {20, 260}, BIG_Y = 222, BIG_W = 200, BIG_H = 170;              /* the settled reveal (matches the host) */
static const int   HULL_Y[2] = {552, 44};                                                  /* hull bar top: you / opponent */
static const int   ENERGY_XY[2][2] = {{200, 596}, {200, 88}};                              /* energy pip centre: you / opponent */
static const int   CREDIT_XY[2][2] = {{372, 566}, {372, 58}};                              /* credits counter */
static const int   ARMOR_XY[2][2] = {{318, 566}, {318, 58}};

typedef struct { float r, g, b; } C3;
static const C3 RED = {225, 80, 66}, YEL = {245, 190, 55}, BLU = {80, 150, 245}, CYAN = {130, 225, 255}, WHT = {245, 245, 250},
                SIL = {190, 198, 210}, GRN = {90, 215, 130}, GOLD = {255, 215, 80}, ORG = {255, 140, 40}, GRY = {110, 114, 128}, DGR = {60, 64, 78},
                SICK = {130, 220, 90};
static const C3 KIND3[3] = {{225, 80, 66}, {245, 190, 55}, {80, 150, 245}};

static FxHost H;
static SDL_Renderer *R;

/* --------------------------------------------------------------------------------------------------- draw primitives */
static float fclampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float ease_out(float x) { x = fclampf(x, 0, 1); return 1 - (1 - x) * (1 - x); }
static float ease_in(float x) { x = fclampf(x, 0, 1); return x * x; }
static float ease_io(float x) { x = fclampf(x, 0, 1); return x < 0.5f ? 2 * x * x : 1 - 2 * (1 - x) * (1 - x); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static C3 mix3(C3 a, C3 b, float t) { C3 c = {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)}; return c; }
static void setc(C3 c, float a) {
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, (Uint8)fclampf(c.r, 0, 255), (Uint8)fclampf(c.g, 0, 255), (Uint8)fclampf(c.b, 0, 255), (Uint8)fclampf(a * 255.0f, 0, 255));
}
static void frect(float x, float y, float w, float h, C3 c, float a) { if (a <= 0) return; SDL_Rect r = {(int)x, (int)y, (int)(w + 0.5f), (int)(h + 0.5f)}; setc(c, a); SDL_RenderFillRect(R, &r); }
static void fline(float x1, float y1, float x2, float y2, float th, C3 c, float a) {
    if (a <= 0) return;
    setc(c, a);
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) { SDL_RenderDrawPoint(R, (int)x1, (int)y1); return; }
    float nx = -dy / len, ny = dx / len; int n = (int)(th + 0.5f); if (n < 1) n = 1;
    for (int i = 0; i < n; i++) {
        float o = (i - (n - 1) * 0.5f);
        SDL_RenderDrawLine(R, (int)(x1 + nx * o), (int)(y1 + ny * o), (int)(x2 + nx * o), (int)(y2 + ny * o));
    }
}
static void fcircle(float cx, float cy, float r, C3 c, float a) {
    if (a <= 0 || r <= 0) return;
    setc(c, a);
    int ir = (int)r + 1;
    for (int y = -ir; y <= ir; y++) {
        float w2 = r * r - (float)y * y; if (w2 < 0) continue;
        float w = sqrtf(w2); SDL_RenderDrawLine(R, (int)(cx - w), (int)(cy + y), (int)(cx + w), (int)(cy + y));
    }
}
static void fring(float cx, float cy, float r, float th, C3 c, float a) {
    if (a <= 0 || r <= 0) return;
    setc(c, a);
    int seg = (int)(r * 0.9f) + 12; if (seg > 90) seg = 90;
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        fline(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, th, c, a);
    }
}
static void fpoly(const float *p, int n, C3 c, float a) {          /* even-odd scanline fill */
    if (a <= 0 || n < 3) return;
    float miny = p[1], maxy = p[1];
    for (int i = 1; i < n; i++) { if (p[2 * i + 1] < miny) miny = p[2 * i + 1]; if (p[2 * i + 1] > maxy) maxy = p[2 * i + 1]; }
    setc(c, a);
    for (int y = (int)miny; y <= (int)maxy; y++) {
        float xs[32]; int k = 0; float fy = y + 0.5f;
        for (int i = 0; i < n && k < 30; i++) {
            int j = (i + 1) % n; float y1 = p[2 * i + 1], y2 = p[2 * j + 1];
            if ((y1 <= fy && y2 > fy) || (y2 <= fy && y1 > fy)) xs[k++] = p[2 * i] + (fy - y1) / (y2 - y1) * (p[2 * j] - p[2 * i]);
        }
        for (int i = 1; i < k; i++) { float v = xs[i]; int j = i - 1; while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
        for (int i = 0; i + 1 < k; i += 2) SDL_RenderDrawLine(R, (int)xs[i], y, (int)xs[i + 1], y);
    }
}
static void hexpts(float cx, float cy, float r, float rot, float *out) { for (int i = 0; i < 6; i++) { float a = rot + 1.0471976f * i; out[2 * i] = cx + cosf(a) * r; out[2 * i + 1] = cy + sinf(a) * r; } }
static void hex_outline(float cx, float cy, float r, float rot, float th, C3 c, float a) {
    float p[12]; hexpts(cx, cy, r, rot, p);
    for (int i = 0; i < 6; i++) { int j = (i + 1) % 6; fline(p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], th, c, a); }
}
static void txt(float x, float y, int scale, C3 c, float a, const char *s) { if (a > 0.02f && H.text) H.text((int)x, (int)y, scale, (uint8_t)c.r, (uint8_t)c.g, (uint8_t)c.b, (uint8_t)(fclampf(a, 0, 1) * 255), s); }
static void txt_c(float cx, float y, int scale, C3 c, float a, const char *s) { int w = H.text_w ? H.text_w(scale, s) : (int)strlen(s) * 6 * scale; txt(cx - w * 0.5f, y, scale, c, a, s); }

/* --------------------------------------------------------------------------------------------------------- particles */
enum { K_SPARK, K_SHARD, K_EMBER, K_SMOKE, K_LINE, K_RING, K_COIN };
typedef struct { float x, y, vx, vy, ax, ay, life, age, size, rot, vrot; C3 c; int kind; } Part;
#define MAXP 700
static Part P[MAXP]; static int np;
static uint32_t rs = 1;
static float rnd01(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float)(rs & 0xFFFFFF) / 16777216.0f; }
static float rr(float a, float b) { return a + (b - a) * rnd01(); }
static void emit(int kind, float x, float y, float vx, float vy, float ax, float ay, float life, float size, C3 c) {
    if (np >= MAXP) return;
    Part *p = &P[np++]; p->x = x; p->y = y; p->vx = vx; p->vy = vy; p->ax = ax; p->ay = ay; p->life = life; p->age = 0; p->size = size; p->rot = rr(0, 6.28f); p->vrot = rr(-9, 9); p->c = c; p->kind = kind;
}
static void burst(float x, float y, int n, float speed, float life, float size, C3 c, int kind) {
    for (int i = 0; i < n; i++) { float a = rr(0, 6.2831853f), s = speed * rr(0.35f, 1.0f); emit(kind, x, y, cosf(a) * s, sinf(a) * s, 0, kind == K_SHARD ? 90 : 60, life * rr(0.7f, 1.2f), size * rr(0.7f, 1.3f), c); }
}
static void parts_update(float dt) {
    for (int i = 0; i < np;) {
        Part *p = &P[i]; p->age += dt; if (p->age < 0) { i++; continue; }
        if (p->age >= p->life) { P[i] = P[--np]; continue; }
        p->vx += p->ax * dt; p->vy += p->ay * dt; p->x += p->vx * dt; p->y += p->vy * dt; p->rot += p->vrot * dt; i++;
    }
}
static void parts_draw(void) {
    for (int i = 0; i < np; i++) {
        Part *p = &P[i]; if (p->age < 0) continue;
        float k = p->age / p->life, a = 1.0f - k * k;
        switch (p->kind) {
        case K_SPARK: frect(p->x - p->size * 0.5f, p->y - p->size * 0.5f, p->size, p->size, p->c, a); break;
        case K_SHARD: {
            float s = p->size, ca = cosf(p->rot), sa = sinf(p->rot);
            float t[6] = {p->x + ca * s, p->y + sa * s, p->x + cosf(p->rot + 2.4f) * s * 0.6f, p->y + sinf(p->rot + 2.4f) * s * 0.6f, p->x + cosf(p->rot + 3.9f) * s * 0.5f, p->y + sinf(p->rot + 3.9f) * s * 0.5f};
            fpoly(t, 3, p->c, a * 0.9f); break;
        }
        case K_EMBER: fcircle(p->x, p->y, p->size * (1.0f - 0.5f * k), mix3(YEL, ORG, k), a); break;
        case K_SMOKE: fcircle(p->x, p->y, p->size * (0.6f + 1.4f * k), p->c, a * 0.35f); break;
        case K_LINE: fline(p->x, p->y, p->x - p->vx * 0.03f, p->y - p->vy * 0.03f, 1.5f, p->c, a); break;
        case K_RING: fring(p->x, p->y, p->size * (0.3f + 1.6f * ease_out(k)), 2, p->c, a * 0.8f); break;
        case K_COIN: fcircle(p->x, p->y, p->size * (0.8f + 0.2f * sinf(p->rot * 3)), p->c, a); break;
        }
    }
}

/* floating labels: damage numbers, glyph flashes ("INTERRUPT", "ABSORBED", ...) */
typedef struct { float x, y, vy, life, age; int scale; C3 c; char s[24]; int bounce; float delay; } Label;
#define MAXL 24
static Label L[MAXL]; static int nl;
static void label(float x, float y, float vy, float life, int scale, C3 c, const char *s, int bounce, float delay) {
    if (nl >= MAXL) return;
    Label *l = &L[nl++]; l->x = x; l->y = y; l->vy = vy; l->life = life; l->age = 0; l->scale = scale; l->c = c; snprintf(l->s, sizeof l->s, "%s", s); l->bounce = bounce; l->delay = delay;
}
static void labels_update(float dt) {
    for (int i = 0; i < nl;) { Label *l = &L[i]; if (l->delay > 0) { l->delay -= dt; i++; continue; } l->age += dt; if (l->age >= l->life) { L[i] = L[--nl]; continue; } l->y += l->vy * dt; i++; }
}
static void labels_draw(void) {
    for (int i = 0; i < nl; i++) {
        Label *l = &L[i]; if (l->delay > 0) continue;
        float k = l->age / l->life, a = k < 0.15f ? k / 0.15f : k > 0.7f ? (1 - k) / 0.3f : 1.0f, pop = 1.0f;
        int sc = l->scale + (k < 0.12f ? 1 : 0);
        float yb = l->bounce ? sinf(fminf(k * 6.0f, 3.14159f)) * -4.0f + fminf(k * 5, 1) * 3 : 0; (void)pop;
        txt_c(l->x + 1, l->y + yb + 1, sc, (C3){0, 0, 0}, a * 0.7f, l->s);
        txt_c(l->x, l->y + yb, sc, l->c, a, l->s);
    }
}

/* ------------------------------------------------------------------------------------------------------- timeline */
typedef enum { SC_NONE, SC_BLITZ, SC_BLOCK, SC_BYPASS, SC_MIRROR_OFF, SC_MIRROR_OPS, SC_MIRROR_DEF, SC_UNOPPOSED, SC_HOLD_SHIELD, SC_BOTH_PASS, SC_CANCEL } Scenario;
static const char *SC_NAME[] = {"none", "blitz", "block", "bypass", "mirror_offense", "mirror_operations", "mirror_defense", "unopposed", "hold_shield", "hold", "cancelled"};
static const char *KW_NAME[] = {"", "lock", "sabotage", "flank", "scan", "siphon"};

typedef struct { float t0, dur, from, to; int on; } Tween;
static struct {
    int active; float t, prev, total, speed;
    Scenario sc; int win, lose, crit, kw;         /* win/lose = seat index; kw = Operations keyword of the winner */
    FxRound r;
    float clash0, clash1, hull0, hull1, armor0, armor1, econ0, econ1, stat0, stat1, settle0;
    int has_hull, has_armor, has_econ, has_stat;
    int cardk[2], cardid[2];                      /* resolved card per seat (-1 pass) */
    int steps;                                    /* combo counter for escalation */
    char name[40];
    Tween tw[4][2];                               /* displayed meter tweens: hull, armor, energy, credits */
    float pulse[4][2];                            /* pulse remaining (ms) */
    int fired[64];
} S;
static float speed_scale = 1.0f;
static FxShown shown, target;
static float shake_amp = 0, shake_t = 0;
static float vignette[2];                         /* red damage vignette on a card */
static float redline_phase = 0; static int redline_you = 0, redline_opp = 0; static float heart_timer = 0;
static float ember_timer[2], regen_timer[2]; static int st_now[2]; static int lock_now;
static float overload[2]; static float overload_x[2];
static float glass_crack[4]; static float glass_age = 99;   /* crack centre + age */
static float emp_flash = 0;

static int ev(int id, float at) { if (S.fired[id]) return 0; if (S.t >= at) { S.fired[id] = 1; return 1; } return 0; }
static int crossed(float at) { return S.prev < at && S.t >= at; }
static float sub(float from, float dur) { return fclampf((S.t - from) / dur, 0, 1); }
static void shake(float amp) { if (amp > shake_amp) shake_amp = amp; }

static void audio(SfxCue c, float at_ms, float pan, float pitch, float inten, int muffle) {
    SfxOpts o = {at_ms / speed_scale, pan, pitch, inten, muffle}; sfx_play(c, o);
}

/* --------------------------------------------------------------------------------------------------------- ships */
static void ship_poly(float x, float y, int dir, float sc, float *out) {
    static const float base[11][2] = {{34, 0}, {6, -10}, {-6, -22}, {-24, -22}, {-14, -6}, {-30, -6}, {-30, 6}, {-14, 6}, {-24, 22}, {-6, 22}, {6, 10}};
    for (int i = 0; i < 11; i++) { out[2 * i] = x + base[i][0] * dir * sc; out[2 * i + 1] = y + base[i][1] * sc; }
}
static void draw_ship(float x, float y, int dir, float sc, C3 accent, float glow, float alpha, float flash) {
    float p[22]; ship_poly(x, y, dir, sc, p);
    if (glow > 0) fcircle(x, y, 40 * sc, accent, 0.10f * glow * alpha);
    fpoly(p, 11, mix3(DGR, WHT, flash * 0.7f), 0.95f * alpha);
    for (int i = 0; i < 11; i++) { int j = (i + 1) % 11; fline(p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], 2, mix3(accent, WHT, flash), alpha); }
    fline(x - 26 * dir * sc, y, x + 24 * dir * sc, y, 1, accent, 0.5f * alpha);
    fcircle(x + 10 * dir * sc, y, 3.2f * sc, mix3(accent, WHT, 0.4f + flash * 0.5f), 0.9f * alpha);
    fcircle(x - 31 * dir * sc, y, 3.5f * sc + glow * 1.5f, accent, 0.55f * alpha);   /* engine glow */
}

static void shield_bubble(float x, float y, float r, C3 c, float alpha, float t_anim) {
    if (r <= 1 || alpha <= 0) return;
    fcircle(x, y, r, c, 0.10f * alpha);
    hex_outline(x, y, r, 0.52f, 3, c, 0.85f * alpha);
    hex_outline(x, y, r * 0.82f, 0.52f, 1, c, 0.35f * alpha);
    float p[12]; hexpts(x, y, r, 0.52f, p);
    for (int i = 0; i < 6; i++) fline(x, y, p[2 * i], p[2 * i + 1], 1, c, 0.18f * alpha);
    float sh = fmodf(t_anim * 0.004f, 1.0f);      /* crystalline shimmer travelling around the rim */
    int k = (int)(sh * 6) % 6; int k2 = (k + 1) % 6;
    fline(p[2 * k], p[2 * k + 1], p[2 * k2], p[2 * k2 + 1], 4, WHT, 0.35f * alpha);
}

static void operations_ui(float x, float y, float t, float alpha, C3 c) {          /* radar dish + spooling ring + data lattice */
    if (alpha <= 0) return;
    float rot = t * 0.006f;
    fring(x, y, 44, 2, c, 0.55f * alpha);
    for (int i = 0; i < 3; i++) { float a = rot + i * 2.0944f; fline(x, y, x + cosf(a) * 44, y + sinf(a) * 44, 2, c, 0.6f * alpha); }
    for (int i = -2; i <= 2; i++) { fline(x - 40, y + i * 14, x + 40, y + i * 14, 1, c, 0.22f * alpha); fline(x + i * 14, y - 40, x + i * 14, y + 40, 1, c, 0.22f * alpha); }
    float a0 = rot * 1.7f; for (int i = 0; i < 6; i++) { float a = a0 + i * 0.12f; fline(x + cosf(a) * 30, y + sinf(a) * 30, x + cosf(a) * 36, y + sinf(a) * 36, 2, WHT, (0.7f - i * 0.1f) * alpha); }
}

/* ---------------------------------------------------------------------------------------------------- flyers (resource particles) */
static void flyers(float x0, float y0, float x1, float y1, int n, C3 c, float dur, float spread, float arc, float delay, int kind) {
    for (int i = 0; i < n; i++) {
        float d = delay + i * (dur * 0.35f / (n > 1 ? n - 1 : 1)), life = dur * rr(0.8f, 1.1f);
        float sx = x0 + rr(-spread, spread), sy = y0 + rr(-spread, spread);
        float dx = x1 - sx, dy = y1 - sy;
        /* analytic arc: v = (dx + arc term) / life via a two-step accel: we approximate with constant velocity + perpendicular accel */
        float vx = dx / life, vy = dy / life;
        float nx = -dy, ny = dx; float nl2 = sqrtf(nx * nx + ny * ny) + 0.001f; nx /= nl2; ny /= nl2;
        float bend = arc * rr(0.6f, 1.2f);
        emit(kind, sx, sy, vx + nx * bend * 0.5f, vy + ny * bend * 0.5f, -nx * bend / life * 0.9f, -ny * bend / life * 0.9f, life, kind == K_COIN ? 4 : 3, c);
        P[np - 1].age = -d;          /* negative age = waiting to launch (parts_update integrates only from age >= 0) */
    }
}

/* ------------------------------------------------------------------------------------------------ scenario building */
static int kind_of(int id) { return id >= 0 ? card_kind(id) : -1; }

static void schedule_audio_and_labels(void);

static void begin_stages(void) {
    /* clash duration by scenario */
    float clash = 1400;
    switch (S.sc) {
    case SC_BLITZ: clash = S.crit ? 1900 : 1500; break;
    case SC_BLOCK: clash = S.crit ? 2000 : 1700; break;
    case SC_BYPASS: clash = S.crit ? 2200 : 2000; break;
    case SC_MIRROR_OFF: case SC_MIRROR_OPS: case SC_MIRROR_DEF: clash = 1400; break;
    case SC_UNOPPOSED: clash = 1200; break;
    case SC_HOLD_SHIELD: clash = 1000; break;
    case SC_BOTH_PASS: clash = 700; break;
    case SC_CANCEL: clash = 1100; break;
    default: break;
    }
    const FxRound *r = &S.r;
    S.has_hull = r->dmg[0] > 0 || r->dmg[1] > 0 || r->heal[0] > 0 || r->heal[1] > 0;
    S.has_armor = r->armor_after[0] != r->armor_before[0] || r->armor_after[1] != r->armor_before[1];
    int vd0 = r->vault_after[0] - r->vault_before[0], vd1 = (r->vault_before[1] == -128 || r->vault_after[1] == -128) ? 0 : r->vault_after[1] - r->vault_before[1];
    S.has_econ = (r->energy_delta[0] != FX_UNKNOWN && r->energy_delta[0] != 0) || (r->energy_delta[1] != FX_UNKNOWN && r->energy_delta[1] != 0) || vd0 != 0 || vd1 != 0;
    S.has_stat = (r->status_after[0] & ~r->status_before[0] & 3) || (r->status_after[1] & ~r->status_before[1] & 3) || (r->flags[1] & 8) || (r->flags[0] & 8) || (r->flags[0] & 16) || (r->flags[1] & 16);
    float t = 600;
    S.clash0 = t; t += clash; S.clash1 = t; t += 220;
    S.hull0 = t; if (S.has_hull) t += 950; S.hull1 = t; if (S.has_hull) t += 120;
    S.armor0 = t; if (S.has_armor) t += 850; S.armor1 = t; if (S.has_armor) t += 100;
    S.econ0 = t; if (S.has_econ) t += 950; S.econ1 = t; if (S.has_econ) t += 100;
    S.stat0 = t; if (S.has_stat) t += 750; S.stat1 = t;
    S.settle0 = t; t += 320;
    S.total = fminf(t, 9000.0f);
}

void fx_begin(const FxRound *rin) {
    if (S.active) fx_finish();
    memset(&S, 0, sizeof S); S.r = *rin; S.active = 1; rs = rin->seed ? rin->seed : 0x1234567u; np = 0; nl = 0; S.speed = speed_scale;
    memset(S.pulse, 0, sizeof S.pulse); shake_amp = 0; vignette[0] = vignette[1] = 0; glass_age = 99;
    const FxRound *r = &S.r;
    int cancelled[2] = { (r->flags[0] & 1) != 0, (r->flags[1] & 1) != 0 };
    for (int s = 0; s < 2; s++) { S.cardid[s] = r->eff[s] >= 0 ? r->eff[s] : (cancelled[s] ? r->card[s] : -1); S.cardk[s] = kind_of(S.cardid[s]); }
    int a = S.cardk[0], b = S.cardk[1];
    S.win = S.lose = -1; S.crit = 0; S.kw = 0;
    if (cancelled[0] || cancelled[1]) S.sc = SC_CANCEL;
    else if (a < 0 && b < 0) S.sc = SC_BOTH_PASS;
    else if (a < 0 || b < 0) { int w = a >= 0 ? 0 : 1; S.win = w; S.lose = 1 - w; S.sc = S.cardk[w] == 2 ? SC_HOLD_SHIELD : SC_UNOPPOSED; }
    else if (a == b) S.sc = a == 0 ? SC_MIRROR_OFF : a == 1 ? SC_MIRROR_OPS : SC_MIRROR_DEF;
    else {
        int w = kind_beats(a, b) ? 0 : 1; S.win = w; S.lose = 1 - w;
        int wk = S.cardk[w];
        S.sc = wk == 0 ? SC_BLITZ : wk == 2 ? SC_BLOCK : SC_BYPASS;
        if (S.sc == SC_BYPASS) S.kw = card_keyword(S.cardid[w]);
        int dmg_l = r->dmg[S.lose];
        if (S.sc == SC_BLITZ) S.crit = dmg_l >= 8;
        else if (S.sc == SC_BLOCK) S.crit = card_power(S.cardid[w]) - card_power(S.cardid[S.lose]) >= 3;
        else S.crit = dmg_l >= 8 || card_cost(S.cardid[w]) >= 5;
    }
    begin_stages();
    snprintf(S.name, sizeof S.name, "%s%s%s%s", SC_NAME[S.sc], S.sc == SC_BYPASS ? "_" : "", S.sc == SC_BYPASS ? KW_NAME[S.kw] : "", S.crit ? "_crit" : "");
    /* meters: keep the OLD values on screen; the resource stages tween to the new ones */
    S.tw[0][0].on = 0;
    schedule_audio_and_labels();
}

/* the sound side of the timeline, on the same clock as the visuals */
static void schedule_audio_and_labels(void) {
    const FxRound *r = &S.r;
    float c0 = S.clash0;
    int emp_me = lock_now != 0;                     /* EMP: everything on my side sounds muffled */
    audio(SFX_FLIP, 0, -0.3f, 0, 1, emp_me);
    audio(SFX_FLIP, 40, 0.3f, 2, 1, 0);
    SfxCue cue = SFX_HOLD;
    switch (S.sc) {
    case SC_BLITZ: cue = S.crit ? SFX_BLITZ_CRIT : SFX_BLITZ; break;
    case SC_BLOCK: cue = S.crit ? SFX_BLOCK_CRIT : SFX_BLOCK; break;
    case SC_BYPASS: cue = S.crit ? SFX_BYPASS_CRIT : S.kw == 1 ? SFX_BYPASS_LOCK : S.kw == 2 ? SFX_BYPASS_SABOTAGE : S.kw == 3 ? SFX_BYPASS_FLANK : S.kw == 4 ? SFX_BYPASS_SCAN : SFX_BYPASS_SIPHON; break;
    case SC_MIRROR_OFF: cue = SFX_MIRROR_OFFENSE; break; case SC_MIRROR_OPS: cue = SFX_MIRROR_OPERATIONS; break; case SC_MIRROR_DEF: cue = SFX_MIRROR_DEFENSE; break;
    case SC_UNOPPOSED: cue = SFX_UNOPPOSED; break; case SC_HOLD_SHIELD: cue = SFX_IMMUNE; break; case SC_BOTH_PASS: cue = SFX_HOLD; break; case SC_CANCEL: cue = SFX_CANCELLED; break;
    default: break;
    }
    float impact = S.sc == SC_BLITZ ? 320 : S.sc == SC_BLOCK ? 120 : S.sc == SC_BYPASS ? 100 : 200;
    (void)impact;
    audio(cue, c0 + 60, S.win >= 0 ? (S.win == 0 ? -0.25f : 0.25f) : 0, 0, 1, 0);
    if (r->flags[0] & 2 || r->flags[1] & 2) audio(SFX_IMMUNE, c0 + 900, 0, 0, 1, 0);
    if (r->flags[0] & 4 || r->flags[1] & 4) audio(SFX_LIFELINE, c0 + 1000, 0, 0, 1, 0);
    if ((r->flags[0] & 16) || (r->flags[1] & 16)) audio(SFX_SWAP, S.stat0, 0, 0, 1, 0);
    /* resources, strictly after the clash stinger has decayed; Hull > Armor > Energy/Credits priority is enforced by the mixer */
    for (int s = 0; s < 2; s++) {
        float pan = s == 0 ? -0.5f : 0.5f;
        int mf = s == 0 && emp_me;
        if (r->dmg[s] > 0) audio(SFX_HULL_HIT, S.hull0 + 60, pan, 0, fclampf(0.45f + r->dmg[s] / 12.0f, 0.45f, 1.4f), mf);
        if (r->heal[s] > 0) audio(SFX_HEAL, S.hull0 + 340, pan, 0, 1, mf);
    }
    int ag[2] = {r->armor_after[0] > r->armor_before[0], r->armor_after[1] > r->armor_before[1]};
    int al[2] = {r->armor_after[0] < r->armor_before[0], r->armor_after[1] < r->armor_before[1]};
    if (ag[0] && ag[1]) { audio(SFX_ARMOR_GAIN, S.armor0 + 60, -0.5f, 0, 1, emp_me); audio(SFX_ARMOR_GAIN, S.armor0 + 60 + 150, 0.5f, 7, 1, 0); }   /* the pitch climb: root, then a fifth up */
    else for (int s = 0; s < 2; s++) if (ag[s]) audio(SFX_ARMOR_GAIN, S.armor0 + 60, s == 0 ? -0.5f : 0.5f, 0, 1, s == 0 && emp_me);
    for (int s = 0; s < 2; s++) if (al[s]) audio(SFX_ARMOR_LOSS, S.armor0 + 80 + s * 140, s == 0 ? -0.5f : 0.5f, 0, 1, s == 0 && emp_me);
    int vd[2] = {r->vault_after[0] - r->vault_before[0], (r->vault_before[1] == -128 || r->vault_after[1] == -128) ? 0 : r->vault_after[1] - r->vault_before[1]};
    int eg[2] = {r->energy_delta[0] != FX_UNKNOWN && r->energy_delta[0] > 0, r->energy_delta[1] != FX_UNKNOWN && r->energy_delta[1] > 0};
    int drain = (r->energy_delta[0] != FX_UNKNOWN && r->energy_delta[0] < 0) || (r->energy_delta[1] != FX_UNKNOWN && r->energy_delta[1] < 0) || vd[0] < -1 || vd[1] < -1;
    float e0 = S.econ0 + 60;
    if (eg[0] && eg[1]) { audio(SFX_ENERGY_GAIN, e0, -0.5f, 0, 1, emp_me); audio(SFX_ENERGY_GAIN, e0 + 150, 0.5f, 7, 1, 0); }
    else for (int s = 0; s < 2; s++) if (eg[s]) audio(SFX_ENERGY_GAIN, e0, s == 0 ? -0.5f : 0.5f, 0, 1, s == 0 && emp_me);
    for (int s = 0; s < 2; s++) if (vd[s] > 0) audio(SFX_CREDITS_GAIN, e0 + 280 + s * 150, s == 0 ? -0.5f : 0.5f, s * 7, 1, s == 0 && emp_me);
    if (drain && (S.win >= 0)) audio(SFX_SIPHON_STREAM, e0 + 100, S.win == 0 ? 0.6f : -0.6f, 0, 1, 0);
    else if (drain) audio(SFX_RESOURCE_LOSS, e0 + 100, 0, 0, 1, 0);
    for (int s = 0; s < 2; s++) if (r->energy_capped[s]) audio(SFX_OVERFLOW, e0 + 200, s == 0 ? -0.5f : 0.5f, 0, 1, 0);
    /* statuses */
    for (int s = 0; s < 2; s++) if ((r->status_after[s] & 1) && !(r->status_before[s] & 1)) audio(SFX_BURN_START, S.stat0 + 40, s == 0 ? -0.4f : 0.4f, 0, 1, s == 0 && emp_me);
    if (r->flags[1] & 8) audio(SFX_EMP, S.stat0 + 20, -0.3f, 0, 1, 0);
    /* labels that name what happened */
    if (S.sc == SC_BLITZ) label(CARD_SX[S.lose] + CARD_SW * 0.5f, CARD_SY + 60, -0.01f, 900, 2, RED, "INTERRUPT", 0, c0 + 620);
    if (S.sc == SC_BLOCK) label(CARD_SX[S.win] + CARD_SW * 0.5f, CARD_SY + 60, -0.01f, 950, 2, CYAN, r->dmg[S.lose] > 0 ? "REFLECTED" : "ABSORBED", 0, c0 + 700);
    if (S.sc == SC_BYPASS) {
        static const char *g[] = {"BYPASS", "LOCKED", "SABOTAGE", "FLANKED", "EXPOSED", "SIPHON"};
        label(CARD_SX[S.lose] + CARD_SW * 0.5f, CARD_SY + 60, -0.01f, 1000, 2, YEL, g[S.kw], 0, c0 + (S.kw == 4 ? 900 : S.kw == 3 ? 1150 : 1000));
    }
    if (S.sc == SC_MIRROR_DEF) label(240, 260, -0.01f, 800, 2, CYAN, "STALEMATE", 0, c0 + 700);
    if (S.sc == SC_HOLD_SHIELD) label(CARD_SX[S.win] + CARD_SW * 0.5f, CARD_SY + 60, -0.01f, 800, 2, CYAN, "HOLD", 0, c0 + 400);
    if (S.sc == SC_BOTH_PASS) label(240, 270, -0.01f, 600, 2, GRY, "BOTH HOLD  +1 ENERGY", 0, c0 + 150);
    if (S.sc == SC_UNOPPOSED) label(CARD_SX[S.lose] + CARD_SW * 0.5f, CARD_SY + 60, -0.01f, 800, 2, RED, "UNOPPOSED", 0, c0 + 500);
    for (int s = 0; s < 2; s++) {
        float cx = CARD_SX[s] + CARD_SW * 0.5f;
        if (r->flags[s] & 1) label(cx, CARD_SY + 20, -0.01f, 1000, 2, GRY, "CANCELLED", 0, c0 + 300);
        if (r->flags[s] & 2) label(cx, CARD_SY + 100, -0.01f, 900, 2, GOLD, "IMMUNE", 0, c0 + 900);
        if (r->flags[s] & 4) label(cx, CARD_SY + 100, -0.01f, 900, 2, RED, "LIFELINE", 0, c0 + 1000);
        if (r->flags[s] & 128) label(cx, CARD_SY + 80, -0.01f, 900, 2, CYAN, "COPIED", 0, c0 + 500);
        if (r->flags[s] & 8) label(CARD_SX[1 - s] + CARD_SW * 0.5f, CARD_SY + 100, -0.01f, 900, 2, GRY, "DISABLED", 0, S.stat0 + 100);
        if (r->flags[s] & 16) label(240, 250, -0.01f, 900, 2, YEL, "HANDS SWAPPED", 0, S.stat0 + 100);
    }
}

/* ------------------------------------------------------------------------------------------------ scenario update */
static void impact_spark(float x, float y, C3 c, int n) { burst(x, y, n, 170, 0.45f, 3, c, K_SPARK); burst(x, y, n / 2, 110, 0.7f, 3, GRY, K_SPARK); }

static void upd_scenario(void) {
    float c0 = S.clash0; float tc = S.t - c0; int W = S.win, Lo = S.lose;
    float wx = W >= 0 ? SHIP_X[W] : 0, lx = Lo >= 0 ? SHIP_X[Lo] : 0, y = SHIP_Y;
    switch (S.sc) {
    case SC_BLITZ:
        for (int i = 0; i < 5; i++) if (ev(10 + i, c0 + 180 + i * 90 + 280)) { impact_spark(lx, y + (i - 2) * 6, RED, 10); shake(S.crit ? 5.0f : 2.5f); }
        if (ev(20, c0 + 560)) { for (int i = 0; i < 16; i++) { float a = rr(0, 6.28f); emit(K_SHARD, lx + cosf(a) * 30, y + sinf(a) * 30, cosf(a) * 150, sinf(a) * 150 - 20, 0, 260, 0.85f, 7, YEL); } shake(3); }
        if (S.crit && ev(21, c0 + 700)) { shake(10.0f); glass_crack[0] = SHIP_X[Lo] + (Lo == 0 ? -40 : 40); glass_crack[1] = y; glass_age = 0; burst(lx + (Lo == 0 ? -34 : 34), y, 24, 240, 0.9f, 4, ORG, K_SPARK); }
        break;
    case SC_BLOCK:
        if (ev(10, c0 + 520)) { impact_spark(wx + (W == 0 ? 48 : -48), y, CYAN, 14); emit(K_RING, wx + (W == 0 ? 48 : -48), y, 0, 0, 0, 0, 0.6f, 26, CYAN); shake(2.5f); }
        if (S.r.dmg[Lo] > 0 && ev(11, c0 + 900)) { impact_spark(lx, y, RED, 10); vignette[Lo] = 0.5f; }
        if (S.crit && ev(12, c0 + 640)) {   /* the projectile is caught, crushed, funnelled into the defender's energy pool */
            float tx = ENERGY_XY[W][0], ty = ENERGY_XY[W][1];
            flyers(wx + (W == 0 ? 48 : -48), y, tx, ty, 14, CYAN, 0.9f, 4, W == 0 ? 60 : -60, 0.05f, K_SPARK);
        }
        break;
    case SC_BYPASS: {
        float sx = wx, dx = lx;
        if (S.kw == 2 && ev(10, c0 + 800)) { impact_spark(dx + (Lo == 0 ? 6 : -6), y - 14, YEL, 8); }
        if (S.kw == 2 && ev(11, c0 + 1150)) { for (int i = 0; i < 12; i++) { float a = i * 0.5236f; emit(K_SHARD, dx + cosf(a) * 46, y + sinf(a) * 46, cosf(a) * 60, sinf(a) * 60, 0, 120, 0.8f, 8, CYAN); } shake(3); }
        if (S.kw == 2 && crossed(c0 + 1300)) burst(dx, y, 6, 90, 0.35f, 2, WHT, K_SPARK);
        if (S.kw == 1 && ev(12, c0 + 1000)) { shake(2); emit(K_RING, dx, y, 0, 0, 0, 0, 0.5f, 30, YEL); }
        if (S.kw == 3) { if (crossed(c0 + 500) || (tc > 500 && tc < 1200 && ((int)tc / 25) != ((int)(tc - 16) / 25))) emit(K_SPARK, wx + (W == 0 ? -40 : 40), y - lerpf(0, 90, sinf(fclampf(tc - 500, 0, 700) / 700 * 3.14159f)), 0, 0, 0, 0, 0.35f, 4, YEL); }
        if (S.kw == 3 && ev(13, c0 + 1200)) { impact_spark(dx + (Lo == 0 ? -30 : 30), y, YEL, 12); shake(2); }
        if (S.kw == 4 && ev(14, c0 + 400)) emit(K_RING, sx, y, 0, 0, 0, 0, 0.9f, 60, YEL);
        if (S.kw == 5 && S.t > c0 + 500 && S.t < c0 + 1500 && ((int)S.t / 40) != ((int)S.prev / 40)) {
            float u = rnd01(); emit(K_SPARK, lerpf(dx, sx, 0.15f), y + rr(-8, 8), (sx - dx) * 1.1f, rr(-20, 20), 0, 0, 0.6f, 4, YEL); (void)u;
        }
        if (S.crit && ev(15, c0 + 1100)) { shake(6); burst(dx, y, 26, 200, 0.8f, 4, YEL, K_SPARK); }
        (void)sx; break;
    }
    case SC_MIRROR_OFF: if (ev(10, c0 + 420)) { burst(240, y, 26, 190, 0.6f, 4, ORG, K_SPARK); shake(4); } break;
    case SC_MIRROR_OPS: if (ev(10, c0 + 420)) { burst(240, y, 22, 160, 0.6f, 3, YEL, K_SPARK); shake(2); } break;
    case SC_MIRROR_DEF: if (ev(10, c0 + 500)) { emit(K_RING, 240, y, 0, 0, 0, 0, 0.7f, 40, CYAN); burst(240, y, 12, 90, 0.5f, 3, CYAN, K_SPARK); } break;
    case SC_UNOPPOSED: for (int i = 0; i < 3; i++) if (ev(10 + i, c0 + 420 + i * 80)) impact_spark(lx, y + (i - 1) * 8, RED, 8); break;
    default: break;
    }
    if (S.sc != SC_NONE && S.t >= c0 && ev(30, c0 + 1)) { /* nothing: scenario armed */ }
}

/* -------------------------------------------------------------------------------------------------- resource stages */
static void start_tween(int meter, int seat, float to, float t0, float dur) { Tween *w = &S.tw[meter][seat]; float from = ((float *)&shown)[meter * 2 + seat]; w->t0 = t0; w->dur = dur; w->from = from; w->to = to; w->on = 1; }

static void upd_resources(void) {
    const FxRound *r = &S.r;
    /* hull */
    if (S.has_hull && S.t >= S.hull0) {
        for (int s = 0; s < 2; s++) {
            if (ev(40 + s, S.hull0 + 40)) {
                float cx = CARD_SX[s] + CARD_SW * 0.5f;
                if (r->dmg[s] > 0) {
                    char b[16]; snprintf(b, sizeof b, "-%d", r->dmg[s]);
                    label(cx, CARD_SY - 34, 0.03f, 900, r->dmg[s] >= 8 ? 4 : 3, RED, b, 1, 0);
                    shake(fclampf(1.5f + r->dmg[s] * 0.55f, 1.5f, 9));
                    float bx = 20 + 440 * fclampf(r->hull_after[s] / 20.0f, 0, 1), by = HULL_Y[s] + 14;
                    burst(bx, by, 6 + r->dmg[s], 130, 0.5f, 3, RED, K_SPARK);
                    for (int i = 0; i < 3 + r->dmg[s] / 3; i++) emit(K_SHARD, bx, by, rr(-40, 40), rr(-80, -20), 0, 200, 0.6f, 5, (C3){170, 40, 35});   /* the bar cracks and fragments */
                    if (r->hull_after[s] <= 4) vignette[s] = 1.0f;
                    if (r->hull_after[s] > 0 && r->hull_after[s] <= 4) redline_phase = 0;
                    S.pulse[0][s] = 400;
                }
                if (r->heal[s] > 0) {
                    char b[16]; snprintf(b, sizeof b, "+%d", r->heal[s]); label(cx, CARD_SY + 20, -0.04f, 900, 3, GRN, b, 0, r->dmg[s] > 0 ? 350 : 0);
                    for (int i = 0; i < 8; i++) emit(K_EMBER, cx + rr(-30, 30), CARD_SY + 110, 0, rr(-40, -15), 0, 0, 0.9f, 2.5f, GRN);
                }
                start_tween(0, s, (float)r->hull_after[s], S.hull0 + 100, 700);
            }
        }
    }
    /* armor: gain = plating slides in; loss = corrosion (dissolves into sick-green vapour) */
    if (S.has_armor && S.t >= S.armor0) {
        for (int s = 0; s < 2; s++) {
            int d = r->armor_after[s] - r->armor_before[s]; if (d == 0) continue;
            if (ev(44 + s, S.armor0 + 30)) {
                float cx = CARD_SX[s] + CARD_SW * 0.5f; char b[24];
                if (d > 0) { snprintf(b, sizeof b, "+%d ARMOR", d); label(cx, CARD_SY + 30, -0.035f, 850, 2, SIL, b, 0, 120); S.pulse[1][s] = 400; }
                else {
                    snprintf(b, sizeof b, "-%d ARMOR", -d); label(cx, CARD_SY + 30, 0.02f, 900, 2, SICK, b, 1, 60);
                    for (int i = 0; i < 16; i++) emit(K_SMOKE, ARMOR_XY[s][0] + rr(-14, 14), ARMOR_XY[s][1] + rr(-4, 8), rr(-6, 6), rr(-34, -12), 0, 0, 0.9f, 4, SICK);
                    for (int i = 0; i < 4; i++) emit(K_SPARK, ARMOR_XY[s][0] + rr(-10, 10), ARMOR_XY[s][1] + 8, 0, rr(20, 60), 0, 120, 0.7f, 3, SICK);   /* melting digits drip */
                }
                start_tween(1, s, (float)r->armor_after[s], S.armor0 + 100, 600);
            }
        }
    }
    /* energy + credits (one stage; siphons flow toward the winner, gains fly in from the centre) */
    if (S.has_econ && S.t >= S.econ0) {
        float speedf = powf(1.2f, (float)S.steps);
        for (int s = 0; s < 2; s++) {
            int ed = r->energy_delta[s]; int vd = (r->vault_before[s] == -128 || r->vault_after[s] == -128) ? 0 : r->vault_after[s] - r->vault_before[s];
            if (ev(48 + s, S.econ0 + 30 + s * 40)) {
                int e_other = r->energy_delta[1 - s]; int siph_from_other = ed != FX_UNKNOWN && ed > 0 && e_other != FX_UNKNOWN && e_other < 0;
                float dur = 0.7f / speedf;
                if (ed != FX_UNKNOWN && ed > 0) {
                    float sx = siph_from_other ? ENERGY_XY[1 - s][0] : 240, sy = siph_from_other ? ENERGY_XY[1 - s][1] : (s == 0 ? 470 : 300);
                    flyers(sx, sy, ENERGY_XY[s][0], ENERGY_XY[s][1], 12, YEL, dur, 10, s == 0 ? -40 : 40, 0.02f, K_SPARK);
                    S.pulse[2][s] = 500; if (siph_from_other || S.sc == SC_BYPASS) S.steps++;
                    if (r->energy_capped[s]) { overload[s] = 800; overload_x[s] = ENERGY_XY[s][0] + 90; for (int i = 0; i < 14; i++) emit(K_SPARK, overload_x[s], ENERGY_XY[s][1], rr(-90, 90), rr(-120, -20), 0, 260, 0.55f, 3, YEL); }
                }
                if (ed != FX_UNKNOWN && ed < 0) { int to = S.win >= 0 && S.win != s ? S.win : 1 - s; flyers(ENERGY_XY[s][0], ENERGY_XY[s][1], ENERGY_XY[to][0], ENERGY_XY[to][1], 10, YEL, dur, 8, 30, 0.02f, K_SPARK); S.pulse[2][s] = 400; S.steps++; }
                if (vd > 0) { flyers(240, s == 0 ? 470 : 300, CREDIT_XY[s][0], CREDIT_XY[s][1], 10, GOLD, 0.75f / speedf, 10, s == 0 ? 40 : -40, 0.25f, K_COIN); S.pulse[3][s] = 600; }
                if (vd < -1) { int to = S.win >= 0 && S.win != s ? S.win : 1 - s; flyers(CREDIT_XY[s][0], CREDIT_XY[s][1], CREDIT_XY[to][0], CREDIT_XY[to][1], 9, GRN, 0.75f / speedf, 6, 30, 0.25f, K_COIN); S.pulse[3][s] = 400; S.steps++; }
                start_tween(2, s, ((const float *)&target)[2 * 2 + s], S.econ0 + 200, 650);
                start_tween(3, s, (float)((r->vault_after[s] == -128) ? target.vault[s] : r->vault_after[s]), S.econ0 + 300, 600);
            }
        }
    }
    /* status ignition / disable */
    if (S.has_stat && S.t >= S.stat0) {
        for (int s = 0; s < 2; s++) {
            if ((r->status_after[s] & 1) && !(r->status_before[s] & 1) && ev(52 + s, S.stat0 + 40)) {
                for (int i = 0; i < 24; i++) emit(K_EMBER, 30 + rr(0, 420), HULL_Y[s] + rr(6, 24), rr(-8, 8), rr(-50, -18), 0, 0, rr(0.8f, 1.3f), rr(1.8f, 3.2f), ORG);
                label(240, HULL_Y[s] + (s == 0 ? -20 : 36), -0.03f, 900, 2, ORG, "BURNING", 0, 0);
            }
        }
        if ((r->flags[1] & 8) && ev(54, S.stat0 + 20)) { emp_flash = 700; shake(4); }
    }
}

/* ----------------------------------------------------------------------------------------------------- update/draw */
void fx_init(const FxHost *h) { H = *h; R = h->R; rs = 1; }
void fx_set_speed(float sp) { speed_scale = sp > 0.01f ? sp : 1.0f; }
void fx_reset(void) {
    memset(&S, 0, sizeof S); np = nl = 0; memset(&shown, 0, sizeof shown); memset(&target, 0, sizeof target); shake_amp = 0; vignette[0] = vignette[1] = 0;
    st_now[0] = st_now[1] = 0; lock_now = 0; emp_flash = 0; overload[0] = overload[1] = 0; redline_you = redline_opp = 0; glass_age = 99;
    sfx_stop_all();
}
int fx_active(void) { return S.active; }
int fx_elapsed_ms(void) { return (int)S.t; }
float fx_total_ms(void) { return S.total / speed_scale; }
const char *fx_scenario_name(void) { return S.name; }
FxShown fx_shown(void) { return shown; }
float fx_pulse(int seat, int meter) { float p = S.pulse[meter][seat]; return p > 0 ? fclampf(p / 500.0f, 0, 1) : 0; }
void fx_get_shake(int *dx, int *dy) { *dx = (int)(shake_amp * sinf(shake_t * 0.11f)); *dy = (int)(shake_amp * cosf(shake_t * 0.137f)); }
void fx_set_meters(const float hull[2], const float armor[2], const float energy[2], const float vault[2], int snap) {
    for (int s = 0; s < 2; s++) { target.hull[s] = hull[s]; target.armor[s] = armor[s]; target.energy[s] = energy[s]; target.vault[s] = vault[s]; }
    if (snap || !S.active) shown = target;
}
void fx_finish(void) { if (!S.active) return; shown = target; S.active = 0; memset(S.pulse, 0, sizeof S.pulse); }
void fx_set_redline(int hy, int ho) {
    int ry = hy > 0 && hy * 5 <= 20, ro = ho > 0 && ho * 5 <= 20;
    if (ry && !redline_you) { audio(SFX_REDLINE_ALARM, 0, -0.2f, 0, 1, 0); heart_timer = 0; }
    redline_you = ry; redline_opp = ro;
}
void fx_status_changed(int sy, int so, int lock_mask, int new_round) {
    if (new_round) {
        float after = S.active ? (S.total - S.t) / speed_scale : 0;      /* the burning turn's crackle starts once the round's animation is done */
        if (sy & 1) audio(SFX_BURN_TICK, after * speed_scale, -0.3f, 0, 1, lock_mask != 0);
        if (so & 1) audio(SFX_BURN_TICK, after * speed_scale, 0.3f, 0, 0.6f, 0);
    }
    st_now[0] = sy; st_now[1] = so; lock_now = lock_mask;
}

void fx_update(unsigned dt_ms) {
    float dt = dt_ms / 1000.0f;
    shake_t += dt_ms; if (shake_amp > 0) { shake_amp *= expf(-dt * 7.0f); if (shake_amp < 0.3f) shake_amp = 0; }
    for (int s = 0; s < 2; s++) { if (vignette[s] > 0) vignette[s] = fmaxf(0, vignette[s] - dt * 1.4f); if (overload[s] > 0) overload[s] -= dt_ms; }
    if (emp_flash > 0) emp_flash -= dt_ms;
    if (glass_age < 90) glass_age += dt;
    redline_phase += dt;
    if (redline_you) { heart_timer -= dt; if (heart_timer <= 0) { heart_timer = 0.95f; audio(SFX_HEARTBEAT, 0, 0, 0, 1, lock_now != 0); } }
    for (int s = 0; s < 2; s++) {   /* persistent status embers + sparkles */
        if (st_now[s] & 1) { ember_timer[s] -= dt; while (ember_timer[s] <= 0) { ember_timer[s] += 0.09f; emit(K_EMBER, 24 + rr(0, 430), HULL_Y[s] + 26 + rr(-6, 4), rr(-6, 6), rr(-42, -18), 0, 0, rr(0.9f, 1.5f), rr(1.6f, 2.8f), ORG); } }
        if (st_now[s] & 2) { regen_timer[s] -= dt; while (regen_timer[s] <= 0) { regen_timer[s] += 0.14f; emit(K_EMBER, 24 + rr(0, 430), HULL_Y[s] + 24, rr(-4, 4), rr(-30, -12), 0, 0, 1.0f, 2.0f, GRN); } }
    }
    float sdt = dt * speed_scale;
    if (!S.active) { parts_update(dt); labels_update((float)dt_ms); return; }
    S.prev = S.t; S.t += dt_ms * speed_scale;
    parts_update(sdt);
    labels_update(dt_ms * speed_scale);
    upd_scenario(); upd_resources();
    /* tweens */
    for (int m = 0; m < 4; m++) for (int s = 0; s < 2; s++) {
        Tween *w = &S.tw[m][s]; if (!w->on) continue;
        float k = fclampf((S.t - w->t0) / w->dur, 0, 1), v = lerpf(w->from, w->to, ease_out(k));
        ((float *)&shown)[m * 2 + s] = v; if (k >= 1) w->on = 0;
    }
    for (int m = 0; m < 4; m++) for (int s = 0; s < 2; s++) if (S.pulse[m][s] > 0) S.pulse[m][s] -= dt_ms * speed_scale;
    if (S.t >= S.total) fx_finish();
}

/* ------------------------------------------------------------------------------------------------- drawing scenarios */
static float card_scale_punch(float t) { return t < 300 ? 1.0f : 1.0f + 0.18f * (1 - ease_out((t - 300) / 300.0f)); }

static void draw_cards(void) {
    float t = S.t;
    for (int s = 0; s < 2; s++) {
        float ex = 0, ey = 0;
        if (S.has_hull && S.t >= S.hull0 && S.t < S.hull0 + 500 && S.r.dmg[s] > 0) { float k = 1 - (S.t - S.hull0) / 500.0f; ex = sinf(S.t * 0.09f) * k * (2 + S.r.dmg[s] * 0.5f); ey = cosf(S.t * 0.11f) * k * 2; }
        float settle = ease_io(sub(S.settle0, 300));
        float w = lerpf((float)CARD_SW, (float)BIG_W, settle), h = lerpf((float)CARD_SH, (float)BIG_H, settle);
        float x = lerpf((float)CARD_SX[s], (float)BIG_X[s], settle), y = lerpf((float)CARD_SY, (float)BIG_Y, settle);
        float p = card_scale_punch(t);
        float fw = w * p, fh = h * p; float fx0 = x - (fw - w) * 0.5f + ex, fy0 = y - (fh - h) * 0.5f + ey;
        float fl = t / 350.0f;
        if (fl < 1) {   /* flip: back, then face, squeezed by |cos| */
            float sc = fabsf(cosf(3.14159f * fl * 0.5f + 0.0f)); if (fl < 0.5f) sc = 1 - fl * 2; else sc = fl * 2 - 1;
            float ww = fmaxf(4, fw * sc);
            if (fl < 0.5f) { frect(fx0 + (fw - ww) * 0.5f, fy0, ww, fh, mix3(DGR, GRY, 0.4f), 1); fline(fx0 + fw * 0.5f - ww * 0.3f, fy0 + fh * 0.5f, fx0 + fw * 0.5f + ww * 0.3f, fy0 + fh * 0.5f, 2, YEL, 0.5f); }
            else if (ww < fw * 0.55f) frect(fx0 + (fw - ww) * 0.5f, fy0, ww, fh, S.r.card[s] >= 0 ? KIND3[card_kind(S.r.card[s])] : GRY, 0.9f);   /* mid-flip: a coloured edge, text would just smear */
            else H.card((int)(fx0 + (fw - ww) * 0.5f), (int)fy0, (int)ww, (int)fh, S.r.card[s], 0);
        } else H.card((int)fx0, (int)fy0, (int)fw, (int)fh, S.r.card[s], 0);
        if (vignette[s] > 0) frect(fx0, fy0, fw, fh, RED, 0.35f * vignette[s] * (0.6f + 0.4f * sinf(S.t * 0.03f)));
        /* strike-through when cancelled */
        if (S.r.flags[s] & 1) { float k = ease_out(sub(S.clash0 + 200, 250)); fline(fx0 + 4, fy0 + 4, fx0 + 4 + (fw - 8) * k, fy0 + 4 + (fh - 8) * k, 4, RED, 0.9f); fline(fx0 + fw - 4, fy0 + 4, fx0 + fw - 4 - (fw - 8) * k, fy0 + 4 + (fh - 8) * k, 4, RED, 0.9f); }
        /* armor plating sliding onto the card edges */
        int d = S.r.armor_after[s] - S.r.armor_before[s];
        if (S.has_armor && d > 0 && S.t >= S.armor0) {
            float k = ease_out(sub(S.armor0, 420)), sh = sub(S.armor0 + 350, 380);
            float th = 6;
            frect(fx0 - 20 * (1 - k), fy0, th, fh, SIL, 0.9f * k); frect(fx0 + fw - th + 20 * (1 - k), fy0, th, fh, SIL, 0.9f * k);
            frect(fx0, fy0 - 20 * (1 - k), fw, th, SIL, 0.9f * k); frect(fx0, fy0 + fh - th + 20 * (1 - k), fw, th, SIL, 0.9f * k);
            for (int i = 1; i < 4; i++) { fline(fx0, fy0 + fh * i / 4, fx0 + th, fy0 + fh * i / 4, 1, DGR, 0.7f * k); fline(fx0 + fw - th, fy0 + fh * i / 4, fx0 + fw, fy0 + fh * i / 4, 1, DGR, 0.7f * k); }
            if (sh > 0 && sh < 1) { float bx = fx0 + (fw + 60) * sh - 30; fline(bx, fy0, bx - 30, fy0 + fh, 14, WHT, 0.30f); }   /* metallic sheen sweep */
        }
        if (S.has_armor && d < 0 && S.t >= S.armor0 && S.t < S.armor0 + 700) { float k = sub(S.armor0, 700); frect(fx0, fy0, fw, fh, SICK, 0.22f * (1 - k)); }
    }
}

static void draw_projectiles_line(float x0, float y0, float x1, float y1, float t, float t0, float flight, C3 c, float th) {
    float k = (t - t0) / flight; if (k < 0 || k > 1.25f) return;
    float kk = fclampf(k, 0, 1); float x = lerpf(x0, x1, kk), y = lerpf(y0, y1, kk);
    float tx = lerpf(x0, x1, fclampf(kk - 0.18f, 0, 1)), ty = lerpf(y0, y1, fclampf(kk - 0.18f, 0, 1));
    fline(tx, ty, x, y, th * 2.2f, c, 0.25f); fline(tx, ty, x, y, th, mix3(c, WHT, 0.4f), 0.95f); fcircle(x, y, th * 1.1f, WHT, 0.9f);
}

static void draw_arena_scene(void) {
    float t = S.t, c0 = S.clash0, tc = t - c0; int W = S.win, Lo = S.lose; float y = SHIP_Y;
    /* ship positions and states */
    float posx[2] = {SHIP_X[0], SHIP_X[1]}, posy[2] = {y, y}, glow[2] = {1, 1}, flash[2] = {0, 0}, alpha = ease_out(sub(0, 400)) * (t > S.settle0 ? 1 - sub(S.settle0, 300) : 1);
    C3 acc[2] = {S.cardk[0] >= 0 ? KIND3[S.cardk[0]] : GRY, S.cardk[1] >= 0 ? KIND3[S.cardk[1]] : GRY};
    float shield_r[2] = {0, 0}, shield_a[2] = {0, 0}; C3 shield_c[2] = {BLU, BLU};
    for (int s = 0; s < 2; s++) if (S.cardk[s] == 2) { float k = ease_out(sub(c0, 320)); shield_r[s] = 48 * k; shield_a[s] = k; }
    switch (S.sc) {
    case SC_BLITZ: {
        int w = W, l = Lo; float dir = SHIP_DIR[w];
        if (S.crit) { for (int i = 0; i < 5; i++) draw_projectiles_line(posx[w] + 36 * dir, y + (i - 2) * 6, posx[l] + 130 * dir, y + (i - 2) * 6 + (i - 2) * 3, t, c0 + 180 + i * 90, 300, RED, 3); }
        else for (int i = 0; i < 5; i++) draw_projectiles_line(posx[w] + 36 * dir, y + (i - 2) * 6, posx[l] - 30 * dir, y + (i - 2) * 4, t, c0 + 180 + i * 90, 280, RED, 3);
        if (t < c0 + 560) operations_ui(posx[l], posy[l], t, 0.85f * ease_out(sub(0, 500)), YEL);   /* the Operations UI, spooling until the missiles shatter it */
        if (t > c0 + 360) { float k = sub(c0 + 360, 500); flash[l] = (1 - k) * (0.5f + 0.5f * sinf(t * 0.05f)); posx[l] += SHIP_DIR[w] * 6 * (1 - k) * (1 - k); }
        glow[l] = t < c0 + 700 ? 1.0f : fmaxf(0, 1 - sub(c0 + 700, 400));
        if (t > c0 + 400) { float k = sub(c0 + 400, 800); for (int i = 0; i < 3; i++) fcircle(posx[l] + (i - 1) * 10, posy[l] + (i % 2 ? 6 : -6), 4, (C3){30, 20, 20}, 0.5f * k); }   /* scorch */
        break;
    }
    case SC_BLOCK: {
        int w = W, l = Lo; float dir = SHIP_DIR[l];
        float fx0 = posx[l] + 36 * dir, fx1 = posx[w] - SHIELD_R * dir;
        draw_projectiles_line(fx0, y, fx1, y, t, c0 + 200, 320, RED, 5);
        if (S.r.dmg[l] > 0) draw_projectiles_line(posx[w] - 50 * dir, y, posx[l] + 30 * dir, y, t, c0 + 700, 280, CYAN, 2);   /* the thinner blue return beam */
        if (t > c0 + 520) { float k = sub(c0 + 520, 500); fcircle(posx[w] - SHIELD_R * dir, y, 12 + 24 * ease_out(k), CYAN, 0.55f * (1 - k)); for (int i = 0; i < 3; i++) fring(posx[w], y, shield_r[w] + 6 + i * 10 * k, 2, CYAN, 0.4f * (1 - k)); }
        if (t > c0 + 700 && S.r.dmg[l] > 0) flash[l] = fmaxf(0, 1 - sub(c0 + 900, 300)) * 0.9f;
        if (t > c0 + 900) { float k = sub(c0 + 900, 700); for (int i = 0; i < 3; i++) fcircle(posx[l] + (i - 1) * 9, posy[l] + (i % 2 ? 7 : -7), 4, (C3){25, 18, 18}, 0.55f * k); }
        if (S.crit && t > c0 + 640) {   /* crushed projectile: shrinking then funnelled (particles handled in update) */
            float k = sub(c0 + 520, 240); fcircle(posx[w] - SHIELD_R * dir, y, 10 * (1 - k), RED, 0.9f * (1 - k));
        }
        glow[w] = 1.0f + (t > c0 + 520 && t < c0 + 900 ? 0.6f : 0);
        break;
    }
    case SC_BYPASS: {
        int w = W, l = Lo; float wx = posx[w], lx = posx[l]; float dirw = SHIP_DIR[w];
        float shield_k = shield_r[l];
        float col_t = 0; C3 sc = BLU;
        if (S.crit) { col_t = sub(c0 + 950, 400); sc = mix3(BLU, YEL, col_t); shield_c[l] = sc; }
        switch (S.kw) {
        case 2: {   /* Sabotage: thin spike enters from behind, circuitry lights inside, shield collapses from within */
            float k = sub(c0 + 380, 500);
            if (k > 0 && k < 1.05f) { float kk = fclampf(k, 0, 1); float ax = wx, ay = y - 70; float mx = lerpf(ax, lx - dirw * -8, kk), my = lerpf(ay, y - 14, kk) - sinf(kk * 3.14159f) * 26; fline(lerpf(ax, mx, 0.7f), lerpf(ay, my, 0.7f), mx, my, 2, YEL, 0.8f); fcircle(mx, my, 3.5f, WHT, 0.9f); }
            float ck = sub(c0 + 800, 350);
            if (ck > 0) { float p[22]; ship_poly(lx, y, SHIP_DIR[l], 1.0f, p); for (int i = 0; i < 11; i++) { int j = (i + 3) % 11; fline(p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], 1, YEL, 0.75f * ck * (0.6f + 0.4f * sinf(t * 0.05f + i))); } }
            float fk = sub(c0 + 1000, 450); if (fk > 0) { shield_a[l] *= (fk < 1 ? 0.45f + 0.55f * fabsf(sinf(t * 0.06f)) : 0); }
            if (t > c0 + 1150) { shield_a[l] = 0; }
            break;
        }
        case 1: {   /* Lock: brackets paint the ship, scanning beams, solid LOCKED reticle */
            float k = ease_out(sub(c0 + 340, 420)); float half = lerpf(70, 40, k);
            for (int cx = -1; cx <= 1; cx += 2) for (int cy = -1; cy <= 1; cy += 2) { float bx = lx + cx * half, by = y + cy * half; fline(bx, by, bx - cx * 12, by, 2, YEL, 0.9f); fline(bx, by, bx, by - cy * 12, 2, YEL, 0.9f); }
            float sw = sub(c0 + 340, 500); if (sw < 1) { float bx = lx - 50 + 100 * fabsf(sinf(sw * 6.2831853f)); fline(bx, y - 46, bx, y + 46, 2, YEL, 0.5f); fline(wx, y, bx, y, 1, YEL, 0.35f); }
            if (t > c0 + 900) { float lk = ease_out(sub(c0 + 900, 200)); fring(lx, y, 34 - 8 * lk, 3, YEL, 0.9f); fline(lx - 44, y, lx + 44, y, 1, YEL, 0.8f); fline(lx, y - 44, lx, y + 44, 1, YEL, 0.8f); shield_c[l] = mix3(BLU, GRY, 0.5f * lk); }
            break;
        }
        case 3: {   /* Flank: the attacker slides around into the rear arc and fires a burst into the unshielded aspect */
            float k = ease_io(sub(c0 + 450, 750)); float behind = lx + SHIP_DIR[w] * -1 * -1 * 0;   /* end position: behind the defender */
            float ex = lx - SHIP_DIR[l] * 52;                                                       /* the defender faces its enemy's original side; rear is opposite */
            float sx = lerpf(wx, ex, k), sy = y - sinf(k * 3.14159f) * 92 + (k > 0.5f ? (k - 0.5f) * 0 : 0);
            posx[w] = sx; posy[w] = sy; (void)behind;
            if (k > 0 && k < 1) for (int i = 1; i <= 4; i++) fcircle(sx + SHIP_DIR[w] * -1 * i * 8 * -1, sy + i * 2, 3.5f - i * 0.6f, YEL, 0.5f - i * 0.1f);
            if (t > c0 + 1200) for (int i = 0; i < 3; i++) draw_projectiles_line(sx - SHIP_DIR[l] * 0 + SHIP_DIR[l] * -1 * 30 * -1, sy, lx - SHIP_DIR[l] * 30, y + (i - 1) * 5, t, c0 + 1200 + i * 60, 160, YEL, 3);
            shield_r[l] = SHIELD_R; shield_a[l] = 1 - 0.0f;   /* the shield stays put, facing the wrong way */
            if (t > c0 + 1350) flash[l] = fmaxf(0, 1 - sub(c0 + 1350, 300));
            break;
        }
        case 4: {   /* Scan: a sensor wave washes over the ship, weak points highlight, EXPOSED */
            float k = sub(c0 + 380, 700);
            if (k > 0) { fring(wx, y, 20 + 200 * k * (lx > wx ? 1 : 1), 3, YEL, 0.6f * (1 - k)); fring(wx, y, 10 + 150 * k, 2, YEL, 0.4f * (1 - k)); }
            float wk = sub(c0 + 700, 500);
            if (wk > 0) { const float pts[3][2] = {{-14, -10}, {6, 12}, {-24, 2}}; for (int i = 0; i < 3; i++) { float px = lx + pts[i][0] * SHIP_DIR[l], py = y + pts[i][1]; float pr = 9 + 3 * sinf(t * 0.02f + i); fline(px - pr, py, px, py - pr, 1, YEL, 0.9f * wk); fline(px, py - pr, px + pr, py, 1, YEL, 0.9f * wk); fline(px + pr, py, px, py + pr, 1, YEL, 0.9f * wk); fline(px, py + pr, px - pr, py, 1, YEL, 0.9f * wk); }
                float p[22]; ship_poly(lx, y, SHIP_DIR[l], 1.08f, p); for (int i = 0; i < 11; i++) { int j = (i + 1) % 11; fline(p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], 1, YEL, 0.6f * wk); } }
            break;
        }
        case 5: {   /* Siphon: tethers and clamps latch on; the shield dims and contracts as yellow streams to the attacker */
            float k = ease_out(sub(c0 + 380, 420));
            for (int i = -1; i <= 1; i++) { float sx = wx + dirw * 34, ex2 = lx - dirw * 30; float mx = lerpf(sx, ex2, k), my = y + i * 14 * k; fline(sx, y + i * 6, mx, my, 2, YEL, 0.85f); fcircle(mx, my, 3, WHT, 0.9f * k); }
            float dk = sub(c0 + 700, 900); shield_r[l] = SHIELD_R * (1 - 0.32f * dk); shield_a[l] = 1 - 0.55f * dk;
            break;
        }
        default: break;
        }
        if (S.crit) {   /* Deep System Hijack: the shield's polarity flips, it turns yellow and constricts, crushing the ship with its own defenses */
            float ck = sub(c0 + 1100, 700);
            if (ck > 0) { shield_r[l] = SHIELD_R * (1 - 0.62f * ease_in(ck)); shield_a[l] = 1; flash[l] = 0.5f * (1 - ck) + 0.3f * fabsf(sinf(t * 0.06f)) * ck; hex_outline(lx, y, shield_r[l], 0.52f + ck, 3, YEL, 1); }
        }
        glow[w] = 1.2f;
        (void)shield_k; (void)wx; break;
    }
    case SC_MIRROR_OFF: { float k = sub(c0 + 100, 320); for (int s = 0; s < 2; s++) draw_projectiles_line(posx[s] + 36 * SHIP_DIR[s], y, lerpf(posx[s], 240, 1) , y, t, c0 + 100, 320, RED, 4); (void)k; if (t > c0 + 440) { float kk = sub(c0 + 440, 300); fcircle(240, y, 12 + 26 * kk, ORG, 0.6f * (1 - kk)); flash[0] = flash[1] = 0.6f * (1 - kk); } break; }
    case SC_MIRROR_OPS: { for (int s = 0; s < 2; s++) operations_ui(posx[s], y, t, 0.7f, YEL); float k = sub(c0 + 200, 500); if (k > 0) for (int s = 0; s < 2; s++) fring(posx[s], y, 20 + 90 * k, 2, YEL, 0.6f * (1 - k)); if (t > c0 + 450) for (int i = 0; i < 10; i++) { float a = rr(0, 0); (void)a; fline(240 - 40 + (i * 37 + (int)(t / 30)) % 80, y - 22 + (i * 13) % 44, 240 - 30 + (i * 53 + (int)(t / 30)) % 60, y - 20 + (i * 17) % 40, 1, WHT, 0.5f); } break; }
    case SC_MIRROR_DEF: { float k = ease_out(sub(c0, 500)); for (int s = 0; s < 2; s++) { shield_r[s] = SHIELD_R + 26 * k; shield_a[s] = 1; } break; }
    case SC_UNOPPOSED: { int w = W, l = Lo; for (int i = 0; i < 3; i++) draw_projectiles_line(posx[w] + 36 * SHIP_DIR[w], y + (i - 1) * 8, posx[l] - 30 * SHIP_DIR[w], y + (i - 1) * 8, t, c0 + 200 + i * 80, 260, S.cardk[w] == 1 ? YEL : RED, 3); glow[l] = 0.3f; if (t > c0 + 460) flash[l] = fmaxf(0, 1 - sub(c0 + 460, 300)); break; }
    case SC_HOLD_SHIELD: { int w = W; shield_r[w] = SHIELD_R * ease_out(sub(c0, 300)); shield_a[w] = 1; break; }
    default: break;
    }
    for (int s = 0; s < 2; s++) {
        float a = alpha; draw_ship(posx[s], posy[s], SHIP_DIR[s], 1.0f, acc[s], glow[s], a, flash[s]);
        if (shield_a[s] > 0 && shield_r[s] > 1) shield_bubble(posx[s], posy[s], shield_r[s], shield_c[s], shield_a[s] * a, t);
        if ((S.r.flags[s] & 2) && t > c0 + 850) { float k = sub(c0 + 850, 350); fring(posx[s], posy[s], 46 + 12 * k, 3, GOLD, 0.8f * (1 - k)); }
    }
    (void)tc;
}

void fx_draw_arena(int reveal_you, int reveal_opp, int have_reveal) {
    (void)reveal_you; (void)reveal_opp; (void)have_reveal;
    if (!S.active) return;
    /* arena backdrop */
    frect(0, AY, 480, AH, (C3){12, 14, 20}, 0.55f);
    fline(0, AY, 480, AY, 1, GRY, 0.4f); fline(0, AY + AH, 480, AY + AH, 1, GRY, 0.4f);
    for (int i = 0; i < 12; i++) fline(20 + i * 40, AY + 6, 20 + i * 40, AY + AH - 6, 1, (C3){40, 46, 62}, 0.25f);
    draw_arena_scene();
    draw_cards();
    parts_draw();
    labels_draw();
    if (glass_age < 4) {   /* debris on the device glass */
        float a = fclampf(1 - glass_age / 3.0f, 0, 1);
        for (int i = 0; i < 9; i++) { float ang = i * 0.7f + 0.3f, len = 26 + (i * 17) % 44; fline(glass_crack[0], glass_crack[1], glass_crack[0] + cosf(ang) * len, glass_crack[1] + sinf(ang) * len, 1, WHT, 0.7f * a); }
        fcircle(glass_crack[0], glass_crack[1], 4, ORG, 0.8f * a);
    }
    if (S.sc == SC_BLITZ && S.crit && S.t > S.clash0 + 700) { int l = S.lose; float k = ease_out(sub(S.clash0 + 700, 300)); float hx = CARD_SX[l] + CARD_SW * 0.5f, hy = CARD_SY + CARD_SH * 0.45f;
        fcircle(hx, hy, 15 * k + 3, ORG, 0.7f); fcircle(hx, hy, 8 * k, (C3){20, 10, 10}, 0.95f); fring(hx, hy, 16 * k + 2, 2, YEL, 0.8f); }   /* molten hole punched through the card */
}

void fx_draw_status_panel(int seat, int x, int y, int w, int h) {
    int st = st_now[seat];
    if (st & 1) { /* char and curl at the corners */
        frect(x, y, 18, 3, (C3){30, 18, 12}, 0.8f); frect(x, y, 3, 14, (C3){30, 18, 12}, 0.8f); frect(x + w - 22, y + h - 3, 22, 3, (C3){30, 18, 12}, 0.8f); frect(x + w - 3, y + h - 16, 3, 16, (C3){30, 18, 12}, 0.8f);
        frect(x, y + h - 3, w, 3, ORG, 0.25f + 0.15f * sinf(redline_phase * 5));
    }
    if (st & 2) frect(x, y + h - 2, w, 2, GRN, 0.3f + 0.2f * sinf(redline_phase * 4));
    if (seat == 1 ? redline_opp : redline_you) frect(x, y, w, h, RED, 0.12f + 0.12f * sinf(redline_phase * 6.9f));
}

void fx_draw_disabled_card(int x, int y, int w, int h) {
    frect(x, y, w, h, (C3){70, 72, 80}, 0.62f);                          /* desaturate */
    for (int yy = y + ((int)(redline_phase * 30) & 3); yy < y + h; yy += 4) fline(x, yy, x + w, yy, 1, (C3){20, 20, 26}, 0.35f);   /* CRT scanlines */
    int gy = y + ((int)(redline_phase * 60) % (h > 8 ? h - 6 : 1)); frect(x, gy, w, 3, WHT, 0.18f);   /* glitch bar */
    fline(x + 6, y + h * 0.35f, x + w - 6, y + h * 0.45f, 3, (C3){150, 152, 165}, 0.85f); fline(x + 6, y + h * 0.55f, x + w - 6, y + h * 0.65f, 3, (C3){150, 152, 165}, 0.85f);   /* static-lock chains */
    txt_c(x + w * 0.5f, y + h * 0.42f, 2, WHT, 0.9f, "DISABLED");
}

void fx_draw_overlay(void) {
    if (redline_you) {   /* emergency lighting: the screen border pulses red */
        float p = 0.5f + 0.5f * sinf(redline_phase * 6.9f); float a = 0.20f + 0.30f * p;
        frect(0, 0, 480, 5, RED, a); frect(0, 951, 480, 5, RED, a); frect(0, 0, 5, 956, RED, a); frect(476, 0, 5, 956, RED, a);
        frect(0, 0, 480, 22, RED, a * 0.25f); frect(0, 934, 480, 22, RED, a * 0.25f);
    }
    if (emp_flash > 0) { float k = emp_flash / 700.0f; for (int yy = 620; yy < 900; yy += 6) fline(0, yy + (int)(k * 20) % 6, 480, yy + (int)(k * 20) % 6, 1, WHT, 0.25f * k); frect(0, 620, 480, 280, (C3){60, 62, 70}, 0.25f * k); }
    for (int s = 0; s < 2; s++) if (overload[s] > 0) { float k = overload[s] / 800.0f; fring(ENERGY_XY[s][0] + 10, ENERGY_XY[s][1], 60 + 14 * (1 - k), 3, YEL, 0.5f * k); txt_c(overload_x[s] + 20, ENERGY_XY[s][1] - 8, 2, YEL, k, "MAX"); }
}
