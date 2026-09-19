/* dw_gui -- DEADWEIGHT SDL2 card client (Windows + Linux). S503-09 (VS0.5).
 * Hand-written C over core/client.c + core/protocol.c (docs/WIRE_PROTOCOL.md); card legality/labels come from the
 * PARENA-generated core/card_rules.c. Single-threaded: one SDL loop pumps the (non-blocking) socket each frame.
 * No font files: a built-in 5x7 bitmap font. Name-only play works with --no-auth servers; --token adds AUTH.
 *   dw_gui [--name N] [--host H] [--port P] [--token JWT] [--autostart]
 *   dw_gui --selftest --host H --port P [--frames DIR]   (headless smoke: plays one full match, dumps BMPs)
 * NOTE: dwc_connect() is a blocking connect -- fine on LAN/localhost, may freeze the window for a moment on a dead
 * remote host (known limitation, VS0.5). */
#include "client.h"          /* pulls net.h first (must precede other system headers) */
#include "card_rules.h"
#include "card_text.h"
#include "draft.h"
#include "version.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define W 480
#define H 956
#define MAX_LOG 6

typedef struct { uint8_t r, g, b; } Col;
static const Col C_BG = {18, 20, 28}, C_PANEL = {32, 36, 50}, C_TEXT = {235, 235, 240}, C_DIM = {130, 135, 150},
    C_GOOD = {80, 200, 120}, C_BAD = {225, 80, 70}, C_SEL = {255, 255, 255}, C_LOCK = {90, 90, 105};
static const Col KIND_COL[3] = {{215, 70, 60}, {225, 160, 40}, {70, 140, 230}};   /* BURST, TANK, SHIELD */
static const char *KIND_NAME[3] = {"BURST", "TANK", "SHIELD"};

/* ---------- 5x7 bitmap font (classic column-major glyphs; lowercase renders as uppercase) ---------- */
typedef struct { char c; uint8_t col[5]; } Glyph;
static const Glyph FONT[] = {
    {'&', {0x36,0x49,0x55,0x22,0x50}}, {'\'', {0x00,0x05,0x03,0x00,0x00}}, {';', {0x00,0x56,0x36,0x00,0x00}},
    {'$', {0x24,0x2A,0x7F,0x2A,0x12}}, {'=', {0x14,0x14,0x14,0x14,0x14}},
    {' ', {0x00,0x00,0x00,0x00,0x00}}, {'!', {0x00,0x00,0x5F,0x00,0x00}}, {'%', {0x23,0x13,0x08,0x64,0x62}},
    {'+', {0x08,0x08,0x3E,0x08,0x08}}, {'-', {0x08,0x08,0x08,0x08,0x08}}, {'.', {0x00,0x60,0x60,0x00,0x00}},
    {'/', {0x20,0x10,0x08,0x04,0x02}}, {':', {0x00,0x36,0x36,0x00,0x00}}, {'>', {0x00,0x41,0x22,0x14,0x08}},
    {'<', {0x08,0x14,0x22,0x41,0x00}}, {'_', {0x40,0x40,0x40,0x40,0x40}}, {'(', {0x00,0x1C,0x22,0x41,0x00}},
    {')', {0x00,0x41,0x22,0x1C,0x00}}, {',', {0x00,0x80,0x60,0x00,0x00}}, {'?', {0x02,0x01,0x51,0x09,0x06}},
    {'0', {0x3E,0x51,0x49,0x45,0x3E}}, {'1', {0x00,0x42,0x7F,0x40,0x00}}, {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4B,0x31}}, {'4', {0x18,0x14,0x12,0x7F,0x10}}, {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}}, {'7', {0x01,0x71,0x09,0x05,0x03}}, {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1E}},
    {'A', {0x7E,0x11,0x11,0x11,0x7E}}, {'B', {0x7F,0x49,0x49,0x49,0x36}}, {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'D', {0x7F,0x41,0x41,0x22,0x1C}}, {'E', {0x7F,0x49,0x49,0x49,0x41}}, {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'G', {0x3E,0x41,0x49,0x49,0x7A}}, {'H', {0x7F,0x08,0x08,0x08,0x7F}}, {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'J', {0x20,0x40,0x41,0x3F,0x01}}, {'K', {0x7F,0x08,0x14,0x22,0x41}}, {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}}, {'N', {0x7F,0x04,0x08,0x10,0x7F}}, {'O', {0x3E,0x41,0x41,0x41,0x3E}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}}, {'Q', {0x3E,0x41,0x51,0x21,0x5E}}, {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}}, {'T', {0x01,0x01,0x7F,0x01,0x01}}, {'U', {0x3F,0x40,0x40,0x40,0x3F}},
    {'V', {0x1F,0x20,0x40,0x20,0x1F}}, {'W', {0x3F,0x40,0x38,0x40,0x3F}}, {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x07,0x08,0x70,0x08,0x07}}, {'Z', {0x61,0x51,0x49,0x45,0x43}},
};
#define NGLYPH ((int)(sizeof FONT / sizeof FONT[0]))

static SDL_Renderer *R;

static const uint8_t *glyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (int i = 0; i < NGLYPH; i++) if (FONT[i].c == c) return FONT[i].col;
    return glyph('?');
}
static int text_w(int scale, const char *s) { return (int)strlen(s) * 6 * scale; }
static void setc(Col c, int a) { SDL_SetRenderDrawColor(R, c.r, c.g, c.b, (Uint8)a); }
static void rect(int x, int y, int w, int h, Col c) { SDL_Rect r = {x, y, w, h}; setc(c, 255); SDL_RenderFillRect(R, &r); }
static void frame(int x, int y, int w, int h, Col c, int t) {
    rect(x, y, w, t, c); rect(x, y + h - t, w, t, c); rect(x, y, t, h, c); rect(x + w - t, y, t, h, c);
}
static void text(int x, int y, int scale, Col c, const char *fmt, ...) {
    char buf[128]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    setc(c, 255);
    for (const char *p = buf; *p; p++, x += 6 * scale) {
        const uint8_t *g = glyph(*p);
        for (int cx = 0; cx < 5; cx++) for (int cy = 0; cy < 7; cy++)
            if (g[cx] & (1 << cy)) { SDL_Rect d = {x + cx * scale, y + cy * scale, scale, scale}; SDL_RenderFillRect(R, &d); }
    }
}
static void text_c(int cx, int y, int scale, Col c, const char *fmt, ...) {
    char buf[128]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    text(cx - text_w(scale, buf) / 2, y, scale, c, "%s", buf);
}

/* ---------- app state ---------- */
enum { S_MENU, S_QUEUE, S_MATCH, S_END, S_DRAFT };
typedef struct {
    int screen;
    char name[DW_NAME_LEN + 1], host[64], port[8], token[DW_MAX_AUTH_TOKEN + 1];
    int focus;                              /* menu field: 0 name, 1 host, 2 port */
    char err[96];
    DwClient c; int connected, welcomed;
    uint32_t match_id; int seat; char opp[DW_NAME_LEN + 1]; int opp_kind;
    int round, hull_you, hull_opp, energy_you, energy_opp, opp_hand; int8_t hand[4];
    int armor_you, armor_opp, vault_you, vault_opp, lock_mask;
    Uint32 deadline_at;                     /* SDL ticks when the round timer ends (0 = none) */
    int sel;                                /* -2 none, -1 pass, 0..3 card slot */
    int locked;
    int have_reveal, rv_you, rv_opp, rv_dy, rv_do, rv_round;
    char log[MAX_LOG][64]; int nlog;
    int result, reason, waiting;
    int qcount_at_end;
    /* draft mode */
    int mode;                               /* DW_MODE_CARD (random) or DW_MODE_DRAFT */
    int pick_no, offer[2], left[3];         /* current offer and bucket counts remaining (1-of / 2-of / 3-of) */
    int npicks, pk_card[DW_DRAFT_PICKS], pk_mult[DW_DRAFT_PICKS], pend_card, pend_mult;
    int deck_n, deck_id; int8_t deck[DW_DRAFT_DECK]; int dumped_draft;
    /* selftest */
    int selftest, autostart; const char *frames_dir; Uint32 state_at; int dumped_mid, dumped_end, done_ok;
} App;
static App A;

static void logline(const char *fmt, ...) {
    char b[64]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (A.nlog == MAX_LOG) { memmove(A.log[0], A.log[1], (MAX_LOG - 1) * sizeof A.log[0]); A.nlog--; }
    memcpy(A.log[A.nlog++], b, sizeof b);
}
static void to_menu(const char *err) {
    if (A.connected) { dwc_close(&A.c); A.connected = 0; }
    A.screen = S_MENU; A.welcomed = 0;
    snprintf(A.err, sizeof A.err, "%s", err ? err : "");
}
static void send_simple(uint8_t type) { DwMsg m; memset(&m, 0, sizeof m); m.type = type; if (dwc_send(&A.c, &m)) to_menu("Connection lost"); }

static void do_connect(void) {
    if (!A.name[0]) { snprintf(A.err, sizeof A.err, "Enter a name"); return; }
    int port = atoi(A.port);
    if (port <= 0 || port > 65535) { snprintf(A.err, sizeof A.err, "Bad port"); return; }
    if (dwc_connect(&A.c, A.host, port)) { snprintf(A.err, sizeof A.err, "Cannot connect to %s:%d", A.host, port); return; }
    A.connected = 1; A.err[0] = 0;
    DwMsg m; memset(&m, 0, sizeof m);
    m.type = DW_C_HELLO; m.u.hello.proto = DW_PROTO_VERSION; m.u.hello.mode = (uint8_t)A.mode; m.u.hello.kind = DW_KIND_HUMAN;
    memcpy(m.u.hello.name, A.name, strlen(A.name) < DW_NAME_LEN ? strlen(A.name) : DW_NAME_LEN);
    if (dwc_send(&A.c, &m)) { to_menu("Connection lost"); return; }
    if (A.token[0]) {
        size_t tl = strlen(A.token); DwMsg a; memset(&a, 0, sizeof a);
        a.type = DW_C_AUTH; a.u.auth.token_len = (uint16_t)tl; memcpy(a.u.auth.token, A.token, tl);
        if (dwc_send(&A.c, &a)) { to_menu("Connection lost"); return; }
    }
    A.screen = S_QUEUE; A.waiting = 0; A.nlog = 0; A.have_reveal = 0; A.state_at = SDL_GetTicks(); A.npicks = 0; A.deck_n = 0;
}

static void lock_selected(void) {
    if (A.screen != S_MATCH || A.locked || A.sel == -2) return;
    DwMsg m; memset(&m, 0, sizeof m);
    m.type = DW_C_PLAY; m.u.play.match_id = A.match_id; m.u.play.round = (uint8_t)A.round; m.u.play.slot = (int8_t)A.sel;
    if (dwc_send(&A.c, &m)) { to_menu("Connection lost"); return; }
    A.locked = 1;
}
static int slot_legal(int s) { return s >= 0 && s < 4 && A.hand[s] >= 0 && !((A.lock_mask >> s) & 1) && is_legal_play(A.hand[s], A.energy_you, A.vault_you); }
static void select_slot(int s) { if (A.screen == S_MATCH && !A.locked && (s == -1 || slot_legal(s))) A.sel = s; }

static void handle_msg(const DwMsg *m) {
    switch (m->type) {
    case DW_S_WELCOME: A.welcomed = 1; send_simple(DW_C_QUEUE); break;
    case DW_S_QUEUED: A.waiting = m->u.queued.waiting; break;
    case DW_S_DRAFT_OFFER: {
        int pn = m->u.draft_offer.pick_no;
        if (pn == 0) A.npicks = 0;
        else if (pn == A.npicks + 1 && A.npicks < DW_DRAFT_PICKS) { A.pk_card[A.npicks] = A.pend_card; A.pk_mult[A.npicks] = A.pend_mult; A.npicks++; }
        A.pick_no = pn; A.offer[0] = m->u.draft_offer.card[0]; A.offer[1] = m->u.draft_offer.card[1];
        for (int i = 0; i < 3; i++) A.left[i] = m->u.draft_offer.left[i];
        A.screen = S_DRAFT; A.state_at = SDL_GetTicks();
        break;
    }
    case DW_S_DRAFT_DONE:
        A.deck_id = (int)m->u.draft_done.deck_id; A.deck_n = DW_DRAFT_DECK; memcpy(A.deck, m->u.draft_done.cards, DW_DRAFT_DECK);
        A.screen = S_QUEUE; A.waiting = 0; A.state_at = SDL_GetTicks();
        break;
    case DW_S_MATCH_FOUND:
        A.match_id = m->u.match_found.match_id; A.seat = m->u.match_found.seat; A.opp_kind = m->u.match_found.opp_kind;
        snprintf(A.opp, sizeof A.opp, "%s", m->u.match_found.opp_name);
        A.screen = S_MATCH; A.nlog = 0; A.have_reveal = 0; A.round = 0; A.sel = -2; A.locked = 0;
        A.hull_you = A.hull_opp = start_hull(); A.armor_you = A.armor_opp = 0; A.vault_you = A.vault_opp = start_vault(); A.lock_mask = 0;
        A.state_at = SDL_GetTicks(); A.dumped_mid = 0;
        break;
    case DW_S_ROUND_START:
        A.round = m->u.round_start.round; A.hull_you = m->u.round_start.hull_you; A.hull_opp = m->u.round_start.hull_opp;
        A.energy_you = m->u.round_start.energy_you; A.energy_opp = m->u.round_start.energy_opp;
        memcpy(A.hand, m->u.round_start.hand, 4); A.opp_hand = m->u.round_start.opp_hand_size;
        A.armor_you = m->u.round_start.armor_you; A.armor_opp = m->u.round_start.armor_opp;
        A.vault_you = m->u.round_start.vault_you; A.vault_opp = m->u.round_start.vault_opp; A.lock_mask = m->u.round_start.lock_mask;
        A.deadline_at = m->u.round_start.deadline_ms ? SDL_GetTicks() + m->u.round_start.deadline_ms : 0;
        A.sel = -2; A.locked = 0; A.state_at = SDL_GetTicks();
        break;
    case DW_S_PLAY_ACK: A.locked = 1; break;
    case DW_S_PLAY_REJECT:
        if (m->u.reject.reason != DW_REJ_ALREADY_LOCKED) { A.locked = 0; A.sel = -2; }
        logline("PLAY REJECTED (%d)", m->u.reject.reason);
        break;
    case DW_S_ROUND_RESULT:
        A.have_reveal = 1; A.rv_round = m->u.round_result.round; A.rv_you = m->u.round_result.card_you; A.rv_opp = m->u.round_result.card_opp;
        A.rv_dy = m->u.round_result.dmg_you; A.rv_do = m->u.round_result.dmg_opp;
        A.hull_you = m->u.round_result.hull_you; A.hull_opp = m->u.round_result.hull_opp;
        A.rv_you = m->u.round_result.eff_you >= 0 ? m->u.round_result.eff_you : m->u.round_result.card_you;
        A.rv_opp = m->u.round_result.eff_opp >= 0 ? m->u.round_result.eff_opp : m->u.round_result.card_opp;
        logline("R%d  YOU -%d +%d  OPP -%d +%d", m->u.round_result.round, m->u.round_result.dmg_you, m->u.round_result.heal_you,
                m->u.round_result.dmg_opp, m->u.round_result.heal_opp);
        break;
    case DW_S_MATCH_END:
        A.result = m->u.match_end.result; A.reason = m->u.match_end.reason; A.screen = S_END; A.state_at = SDL_GetTicks();
        A.dumped_end = 0; break;
    case DW_S_ERROR: to_menu(m->u.error.code == DW_ERR_AUTH ? "Server rejected login" : "Server error"); break;
    default: break;
    }
}
static void pump(void) {
    DwMsg m;
    while (A.connected) {
        int r = dwc_recv(&A.c, &m, 0);
        if (r == 0) break;
        if (r < 0) { to_menu("Disconnected"); break; }
        handle_msg(&m);
    }
}

/* ---------- drawing ---------- */
static int in_rect(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }
static void button(int x, int y, int w, int h, const char *label, Col c, int enabled, int mx, int my) {
    Col f = enabled ? c : C_LOCK;
    if (enabled && in_rect(mx, my, x, y, w, h)) { f.r = (uint8_t)(f.r + (255 - f.r) / 4); f.g = (uint8_t)(f.g + (255 - f.g) / 4); f.b = (uint8_t)(f.b + (255 - f.b) / 4); }
    rect(x, y, w, h, f); text_c(x + w / 2, y + h / 2 - 10, 3, enabled ? C_TEXT : C_DIM, "%s", label);
}
static void hull_bar(int y, int hull, const char *label, int armor, int vault) {
    int hp = hull < 0 ? 0 : hull, mx = start_hull();
    rect(20, y, 440, 26, C_PANEL);
    rect(20, y, 440 * (hp > mx ? mx : hp) / mx, 26, hp * 3 <= mx ? C_BAD : C_GOOD);
    text(28, y + 5, 2, C_TEXT, "%s %d/%d", label, hull < 0 ? 0 : hull, mx);
    if (armor == DW_HIDDEN_U8) text(300, y + 5, 2, C_TEXT, "A? $?");
    else text(300, y + 5, 2, C_TEXT, "A%d $%d", armor, vault);
}
static void pips(int y, int energy, const char *label) {
    text(20, y + 2, 2, C_DIM, "%s", label);
    if (energy == DW_HIDDEN_U8) { text(112, y + 2, 2, C_DIM, "? (HIDDEN)"); return; }
    for (int i = 0; i < 6; i++) { rect(112 + i * 34, y, 28, 16, i < energy ? (Col){250, 210, 70} : C_PANEL); }
}
static int wrap_next(const char *t, int per) {
    int n = (int)strlen(t);
    if (n > per) { n = per; while (n > 0 && t[n] != ' ') n--; if (n == 0) n = per; }
    return n;
}
static int wrap_lines(const char *t, int per) {
    int c = 0; while (*t) { t += wrap_next(t, per); while (*t == ' ') t++; c++; }
    return c;
}
static void card_box(int x, int y, int w, int h, int id, int num, int state /*0 normal 1 sel 2 disabled 3 locked*/) {
    rect(x, y, w, h, C_PANEL);
    if (id < 0) { frame(x, y, w, h, C_LOCK, 2); text_c(x + w / 2, y + h / 2 - 7, 2, C_DIM, "PASS"); return; }
    Col kc = KIND_COL[card_kind(id)];
    if (state == 2) { kc.r /= 3; kc.g /= 3; kc.b /= 3; }
    Col tc = state == 2 ? C_DIM : C_TEXT;
    int big = w >= 200;
    const char *nm = dw_card_name(id);
    rect(x, y, w, 30, kc);
    if (big && (int)strlen(nm) * 12 <= w - 8) text_c(x + w / 2, y + 8, 2, C_TEXT, "%s", nm);
    else text_c(x + w / 2, y + 12, 1, C_TEXT, "%s", nm);
    int ty;
    if (big) {
        text_c(x + w / 2, y + 36, 2, tc, "COST %d  PWR %d", card_cost(id), card_power(id));
        text_c(x + w / 2, y + 54, 1, C_DIM, "%s%s", KIND_NAME[card_kind(id)], card_credit(id) ? " $" : "");
        ty = y + 68;
    } else {
        text_c(x + w / 2, y + 36, 1, tc, "COST %d  PWR %d", card_cost(id), card_power(id));
        text_c(x + w / 2, y + 48, 1, C_DIM, "%s%s", KIND_NAME[card_kind(id)], card_credit(id) ? " $" : "");
        ty = y + 64;
    }
    const char *t = dw_card_text(id);
    int avail = y + h - 14 - ty, sc = big ? 2 : 1;
    if (sc == 2 && wrap_lines(t, (w - 8) / 12) * 18 > avail) sc = 1;
    int per = (w - 8) / (6 * sc), lh = sc == 2 ? 18 : 10;
    for (int ly = ty; *t && ly + 8 * sc <= y + h - 2; ly += lh) {
        int n = wrap_next(t, per); char ln[48]; snprintf(ln, sizeof ln, "%.*s", n, t);
        text(x + 4, ly, sc, tc, "%s", ln);
        t += n; while (*t == ' ') t++;
    }
    if (num > 0) text(x + 4, y + h - 14, 1, C_DIM, "(%d)", num);
    if (state == 1) frame(x - 3, y - 3, w + 6, h + 6, C_SEL, 3);
    if (state == 3) frame(x, y, w, h, C_GOOD, 2);
}
static void draw_menu(int mx, int my) {
    text_c(W / 2, 90, 5, C_TEXT, "DEADWEIGHT");
    text_c(W / 2, 150, 2, C_DIM, "%s", A.mode == DW_MODE_DRAFT ? "DRAFT MODE" : "CARD MODE  (RANDOM)");
    const char *lab[3] = {"NAME", "HOST", "PORT"}; char *val[3] = {A.name, A.host, A.port};
    for (int i = 0; i < 3; i++) {
        int y = 250 + i * 90;
        text(40, y - 26, 2, C_DIM, "%s", lab[i]);
        rect(40, y, 400, 46, C_PANEL); frame(40, y, 400, 46, i == A.focus ? C_SEL : C_LOCK, 2);
        text(52, y + 13, 3, C_TEXT, "%s%s", val[i], (i == A.focus && (SDL_GetTicks() / 500) % 2) ? "_" : "");
    }
    button(40, 486, 400, 44, A.mode == DW_MODE_DRAFT ? "MODE: DRAFT" : "MODE: RANDOM", C_LOCK, 1, mx, my);
    button(40, 540, 400, 70, "PLAY", C_GOOD, 1, mx, my);
    if (A.err[0]) text_c(W / 2, 640, 2, C_BAD, "%s", A.err);
    text_c(W / 2, 760, 1, C_DIM, "TAB = NEXT FIELD   ENTER = PLAY   F2 = MODE   ESC = QUIT   V%s", DW_VERSION);
}
#define DRAFT_BTN_X(card, m) (20 + (card) * 240 + (m) * 70)
static int draft_mult_ok(int m) { return A.left[m - 1] > 0; }
static void draw_draft(int mx, int my) {
    text_c(W / 2, 20, 3, C_TEXT, "DRAFT  PICK %d/%d", A.pick_no + 1 > DW_DRAFT_PICKS ? DW_DRAFT_PICKS : A.pick_no + 1, DW_DRAFT_PICKS);
    text_c(W / 2, 56, 2, C_DIM, "LEFT  1X: %d   2X: %d   3X: %d", A.left[0], A.left[1], A.left[2]);
    text_c(W / 2, 84, 1, C_DIM, "PICK A CARD AND HOW MANY COPIES: 10 SINGLES, 5 PAIRS, 1 TRIPLE = 23 CARDS");
    for (int c = 0; c < 2; c++) {
        card_box(20 + c * 240, 110, 200, 300, A.offer[c], 0, 0);
        for (int m = 1; m <= 3; m++) {
            char lb[8]; snprintf(lb, sizeof lb, "%dX", m);
            button(DRAFT_BTN_X(c, m - 1), 420, 60, 44, lb, C_GOOD, draft_mult_ok(m), mx, my);
        }
    }
    text(20, 480, 2, C_DIM, "YOUR DECK SO FAR (%d)", A.npicks);
    for (int i = 0; i < A.npicks; i++) text(20, 506 + i * 24, 2, C_TEXT, "%dX %s", A.pk_mult[i], dw_card_name(A.pk_card[i]));
    button(20, 906, 200, 42, "LEAVE", C_LOCK, 1, mx, my);
}
static void draw_queue(int mx, int my) {
    text_c(W / 2, 250, 4, C_TEXT, "SEARCHING...");
    text_c(W / 2, 320, 2, C_DIM, A.welcomed ? "IN QUEUE  (%d WAITING)" : "CONNECTING...", A.waiting);
    text_c(W / 2, 360, 2, C_DIM, "PLAYING AS %s", A.name);
    button(140, 520, 200, 64, "CANCEL", C_BAD, 1, mx, my);
}
static void draw_match(int mx, int my) {
    text(20, 14, 2, C_TEXT, "%s%s", A.opp, A.opp_kind ? " (BOT)" : "");
    hull_bar(44, A.hull_opp, "HULL", A.armor_opp, A.vault_opp); pips(80, A.energy_opp, "ENERGY"); text(350, 82, 2, C_DIM, "HAND %d", A.opp_hand);
    text_c(W / 2, 118, 3, C_TEXT, "ROUND %d/%d", A.round, max_rounds());
    if (A.deadline_at) { int left = (int)(A.deadline_at - SDL_GetTicks()); if (left < 0) left = 0; text_c(W / 2, 148, 2, left < 5000 ? C_BAD : C_DIM, "%d S", left / 1000); }
    if (A.have_reveal) {
        text_c(W / 2, 176, 2, C_DIM, "LAST ROUND (%d)", A.rv_round);
        text_c(120, 200, 2, C_DIM, "YOU"); text_c(360, 200, 2, C_DIM, "OPP");
        card_box(20, 222, 200, 170, A.rv_you, 0, 0); card_box(260, 222, 200, 170, A.rv_opp, 0, 0);
        text_c(120, 398, 2, A.rv_dy ? C_BAD : C_DIM, "TOOK %d", A.rv_dy); text_c(360, 398, 2, A.rv_do ? C_GOOD : C_DIM, "TOOK %d", A.rv_do);
    } else text_c(W / 2, 260, 2, C_DIM, "PICK A CARD OR PASS");
    for (int i = 0; i < A.nlog; i++) text(20, 424 + i * 20, 2, C_DIM, "%s", A.log[i]);
    hull_bar(552, A.hull_you, "YOU", A.armor_you, A.vault_you); pips(588, A.energy_you, "ENERGY");
    for (int i = 0; i < 4; i++) {
        int st = A.hand[i] < 0 ? 2 : !slot_legal(i) ? 2 : (A.sel == i ? (A.locked ? 3 : 1) : 0);
        card_box(20 + (i % 2) * 232, 620 + (i / 2) * 142, 216, 136, A.hand[i], i + 1, st);
    }
    int can = !A.locked && A.sel != -2;
    button(20, 906, 200, 42, A.sel == -1 ? (A.locked ? "PASSED" : "PASS *") : "PASS", C_LOCK, !A.locked, mx, my);
    button(240, 906, 220, 42, A.locked ? "LOCKED" : "LOCK IN", C_GOOD, can, mx, my);
}
static void draw_end(int mx, int my) {
    Col c = A.result == DW_RES_WIN ? C_GOOD : A.result == DW_RES_LOSS ? C_BAD : C_DIM;
    text_c(W / 2, 220, 6, c, "%s", A.result == DW_RES_WIN ? "VICTORY" : A.result == DW_RES_LOSS ? "DEFEAT" : "DRAW");
    const char *why[5] = {"HULL DESTROYED", "ROUNDS OVER", "OPPONENT FORFEIT", "SERVER", "BANKRUPT"};
    text_c(W / 2, 310, 2, C_DIM, "%s", why[A.reason > 4 ? 3 : A.reason]);
    text_c(W / 2, 350, 2, C_TEXT, "YOU %d  -  %s %d", A.hull_you < 0 ? 0 : A.hull_you, A.opp, A.hull_opp < 0 ? 0 : A.hull_opp);
    if (A.mode == DW_MODE_DRAFT && A.deck_n) {
        button(60, 450, 360, 64, "SAME DECK", C_GOOD, 1, mx, my);
        button(60, 525, 360, 64, "REDRAFT", (Col){70, 110, 200}, 1, mx, my);
        text_c(W / 2, 606, 1, C_DIM, "DECK %d", A.deck_id);
    } else button(60, 480, 360, 70, "PLAY AGAIN", C_GOOD, 1, mx, my);
    button(60, 640, 360, 56, "MENU", C_LOCK, 1, mx, my);
}
static void draw(int mx, int my) {
    setc(C_BG, 255); SDL_RenderClear(R);
    switch (A.screen) { case S_MENU: draw_menu(mx, my); break; case S_QUEUE: draw_queue(mx, my); break;
                        case S_MATCH: draw_match(mx, my); break; case S_DRAFT: draw_draft(mx, my); break; default: draw_end(mx, my); break; }
}

/* ---------- input ---------- */
static char *field(int i) { return i == 0 ? A.name : i == 1 ? A.host : A.port; }
static size_t field_cap(int i) { return i == 0 ? DW_NAME_LEN : i == 1 ? sizeof A.host - 1 : 5; }
static void send_pick(int idx, int mult) {
    DwMsg m; memset(&m, 0, sizeof m); m.type = DW_C_DRAFT_PICK; m.u.draft_pick.index = (uint8_t)idx; m.u.draft_pick.mult = (uint8_t)mult;
    A.pend_card = A.offer[idx]; A.pend_mult = mult;
    if (dwc_send(&A.c, &m)) to_menu("Connection lost");
}
/* Back into the queue after a match: same_deck (draft only) replays the last deck, otherwise redraft / plain requeue. */
static void requeue(int same_deck) {
    DwMsg m; memset(&m, 0, sizeof m); m.type = DW_C_QUEUE; m.u.queue.same_deck = (uint8_t)same_deck;
    A.screen = S_QUEUE; A.waiting = 0; A.state_at = SDL_GetTicks();
    if (dwc_send(&A.c, &m)) to_menu("Connection lost");
}
static void click(int x, int y) {
    switch (A.screen) {
    case S_MENU:
        for (int i = 0; i < 3; i++) if (in_rect(x, y, 40, 250 + i * 90, 400, 46)) A.focus = i;
        if (in_rect(x, y, 40, 486, 400, 44)) A.mode = A.mode == DW_MODE_DRAFT ? DW_MODE_CARD : DW_MODE_DRAFT;
        if (in_rect(x, y, 40, 540, 400, 70)) do_connect();
        break;
    case S_QUEUE: if (in_rect(x, y, 140, 520, 200, 64)) { send_simple(DW_C_LEAVE); to_menu(""); } break;
    case S_MATCH:
        for (int i = 0; i < 4; i++) if (in_rect(x, y, 20 + (i % 2) * 232, 620 + (i / 2) * 142, 216, 136)) select_slot(i);
        if (in_rect(x, y, 20, 906, 200, 42)) select_slot(-1);
        if (in_rect(x, y, 240, 906, 220, 42)) lock_selected();
        break;
    case S_DRAFT:
        for (int c = 0; c < 2; c++) for (int m = 1; m <= 3; m++)
            if (in_rect(x, y, DRAFT_BTN_X(c, m - 1), 420, 60, 44) && draft_mult_ok(m)) send_pick(c, m);
        if (in_rect(x, y, 20, 906, 200, 42)) { send_simple(DW_C_LEAVE); to_menu(""); }
        break;
    default:
        if (A.mode == DW_MODE_DRAFT && A.deck_n) {
            if (in_rect(x, y, 60, 450, 360, 64)) requeue(1);
            if (in_rect(x, y, 60, 525, 360, 64)) requeue(0);
        } else if (in_rect(x, y, 60, 480, 360, 70)) requeue(0);
        if (in_rect(x, y, 60, 640, 360, 56)) to_menu("");
        break;
    }
}
static void key(SDL_Keycode k) {
    if (A.screen == S_MENU) {
        char *f = field(A.focus);
        if (k == SDLK_BACKSPACE && *f) f[strlen(f) - 1] = 0;
        else if (k == SDLK_TAB) A.focus = (A.focus + 1) % 3;
        else if (k == SDLK_F2) A.mode = A.mode == DW_MODE_DRAFT ? DW_MODE_CARD : DW_MODE_DRAFT;
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) do_connect();
    } else if (A.screen == S_MATCH) {
        if (k >= SDLK_1 && k <= SDLK_4) select_slot((int)(k - SDLK_1));
        else if (k == SDLK_p) select_slot(-1);
        else if (k == SDLK_SPACE || k == SDLK_RETURN) lock_selected();
    } else if (A.screen == S_END && (k == SDLK_RETURN || k == SDLK_SPACE)) click(100, A.mode == DW_MODE_DRAFT && A.deck_n ? 470 : 500);
}

/* ---------- selftest ---------- */
static void dump(const char *tag) {
    if (!A.frames_dir) return;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    if (SDL_RenderReadPixels(R, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
        char p[512]; snprintf(p, sizeof p, "%s/dw_gui_%s.bmp", A.frames_dir, tag); SDL_SaveBMP(s, p); printf("selftest: wrote %s\n", p);
    }
    SDL_FreeSurface(s);
}
static void selftest_tick(void) {
    Uint32 now = SDL_GetTicks();
    if (A.screen == S_DRAFT && now - A.state_at > 10) {
        if (A.pick_no >= 3 && !A.dumped_draft) { A.dumped_draft = 1; dump("draft"); }
        int m = 1; while (m < 3 && !draft_mult_ok(m)) m++;
        send_pick(0, m); A.state_at = now;
    }
    if (A.screen == S_MATCH && A.round >= 3 && !A.dumped_mid && now - A.state_at > 20) { A.dumped_mid = 1; dump("match"); }
    if (A.screen == S_MATCH && !A.locked && A.round > 0 && now - A.state_at > 40) {
        int pick = -1; for (int s = 0; s < 4; s++) if (slot_legal(s)) { pick = s; break; }
        select_slot(pick); lock_selected();
    }
    if (A.screen == S_END && !A.dumped_end && now - A.state_at > 20) { A.dumped_end = 1; dump("end"); A.done_ok = 1; }
}

int main(int argc, char **argv) {
    snprintf(A.name, sizeof A.name, "Player"); snprintf(A.host, sizeof A.host, "okemily.com"); snprintf(A.port, sizeof A.port, "6980");
    A.sel = -2; for (int i = 0; i < 4; i++) A.hand[i] = -1;
    const char *envt = getenv("DW_TOKEN"); if (envt) snprintf(A.token, sizeof A.token, "%s", envt);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--version")) { printf("dw_gui %s\n", DW_VERSION); return 0; }
        else if (!strcmp(a, "--name") && v) { snprintf(A.name, sizeof A.name, "%s", v); i++; }
        else if (!strcmp(a, "--host") && v) { snprintf(A.host, sizeof A.host, "%s", v); i++; }
        else if (!strcmp(a, "--port") && v) { snprintf(A.port, sizeof A.port, "%s", v); i++; }
        else if (!strcmp(a, "--token") && v) { snprintf(A.token, sizeof A.token, "%s", v); i++; }
        else if (!strcmp(a, "--frames") && v) { A.frames_dir = v; i++; }
        else if (!strcmp(a, "--autostart")) A.autostart = 1;
        else if (!strcmp(a, "--mode") && v) { A.mode = !strcmp(v, "draft") ? DW_MODE_DRAFT : DW_MODE_CARD; i++; }
        else if (!strcmp(a, "--selftest")) A.selftest = A.autostart = 1;
        else { fprintf(stderr, "usage: dw_gui [--name N] [--host H] [--port P] [--token JWT] [--autostart] [--selftest [--frames DIR]]\n"); return 2; }
    }
    if (A.selftest) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    dw_net_init();
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("DEADWEIGHT", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    R = SDL_CreateRenderer(win, -1, A.selftest ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R) R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!R) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(R, W, H);
    SDL_StartTextInput();

    if (A.autostart) do_connect();
    if (A.selftest && A.screen != S_QUEUE) { fprintf(stderr, "selftest: %s\n", A.err); return 1; }

    int running = 1, mx = 0, my = 0, rc = 0; Uint32 t0 = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_MOUSEMOTION) { mx = e.motion.x; my = e.motion.y; }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) click(e.button.x, e.button.y);
            else if (e.type == SDL_TEXTINPUT && A.screen == S_MENU) {
                char *f = field(A.focus); size_t n = strlen(f);
                for (const char *p = e.text.text; *p && n < field_cap(A.focus); p++)
                    if ((unsigned char)*p >= 32 && (unsigned char)*p < 127 && (A.focus != 2 || (*p >= '0' && *p <= '9'))) { f[n++] = *p; f[n] = 0; }
            }
            else if (e.type == SDL_KEYDOWN) { if (e.key.keysym.sym == SDLK_ESCAPE && A.screen == S_MENU) running = 0; else key(e.key.keysym.sym); }
        }
        pump();
        if (A.selftest) {
            selftest_tick();
            if (A.done_ok) { printf("selftest: full match played, result=%d reason=%d\n", A.result, A.reason); break; }
            if (SDL_GetTicks() - t0 > 30000) { fprintf(stderr, "selftest: timeout (screen=%d round=%d err=%s)\n", A.screen, A.round, A.err); rc = 1; break; }
            if (A.screen == S_MENU) { fprintf(stderr, "selftest: bounced to menu: %s\n", A.err); rc = 1; break; }
        }
        draw(mx, my);
        SDL_RenderPresent(R);
        if (A.selftest) SDL_Delay(8); else if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_SHOWN)) SDL_Delay(16);
    }
    if (A.connected) dwc_close(&A.c);
    SDL_DestroyRenderer(R); SDL_DestroyWindow(win); SDL_Quit();
    return rc;
}
