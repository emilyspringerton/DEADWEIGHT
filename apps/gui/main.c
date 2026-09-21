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
#include "iduna.h"
#include "version.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <math.h>
#include "fx.h"
#include "sfx.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define W 480
#define H 956
#define MAX_LOG 6

typedef struct { uint8_t r, g, b; } Col;
static const Col C_BG = {18, 20, 28}, C_PANEL = {32, 36, 50}, C_TEXT = {235, 235, 240}, C_DIM = {130, 135, 150},
    C_GOOD = {80, 200, 120}, C_BAD = {225, 80, 70}, C_SEL = {255, 255, 255}, C_LOCK = {90, 90, 105};
static const Col KIND_COL[3] = {{215, 70, 60}, {225, 160, 40}, {70, 140, 230}};   /* Offense (red), Operations (yellow), Defense (blue) */
static const char *KIND_NAME[3] = {"OFFENSE", "OPERATIONS", "DEFENSE"};

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
static void text_a(int x, int y, int scale, Col c, int alpha, const char *s) {
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    setc(c, alpha);
    for (const char *p = s; *p; p++, x += 6 * scale) {
        const uint8_t *g = glyph(*p);
        for (int cx = 0; cx < 5; cx++) for (int cy = 0; cy < 7; cy++)
            if (g[cx] & (1 << cy)) { SDL_Rect d = {x + cx * scale, y + cy * scale, scale, scale}; SDL_RenderFillRect(R, &d); }
    }
}
static void text(int x, int y, int scale, Col c, const char *fmt, ...) {
    char buf[128]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    text_a(x, y, scale, c, 255, buf);
}
static void text_c(int cx, int y, int scale, Col c, const char *fmt, ...) {
    char buf[128]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    text(cx - text_w(scale, buf) / 2, y, scale, c, "%s", buf);
}

/* ---------- app state ---------- */
enum { S_BOOT, S_MENU, S_QUEUE, S_MATCH, S_END, S_DRAFT, S_DRAFT_HUB };
typedef struct {
    int screen;
    char name[DW_NAME_LEN + 1], host[64], port[8], token[DW_MAX_AUTH_TOKEN + 1];
    int focus;                              /* menu field: 0 name, 1 host, 2 port, 3 redeem code */
    char err[96];
    /* IDUNA zero-friction auth (S508) -- see iduna_bootstrap(). player_id/tickets/is_founder are
     * populated after a successful guest-register or guest-login; A.token above is reused
     * unmodified as the existing HELLO/AUTH flow to dw_server already expects (no change needed
     * there -- this just fills it in automatically instead of requiring --token/DW_TOKEN). */
    DwIduna idu; char account_path[256]; char iduna_url[96];
    char player_id[48]; int tickets, is_founder, auth_ready;
    char redeem_code[24]; char redeem_msg[64];
    char link_email[64], link_pass[32]; char link_msg[64];
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
    /* round-resolution animation plumbing (fx.c) */
    int rs_hull[2], rs_armor[2], rs_vault[2], rs_energy[2], status[2];   /* meters at the start of the current round (you, opp) */
    FxRound pend; int pend_valid, end_wait, muted_hint;
    float sh_hull[2], sh_armor[2], sh_energy[2], sh_vault[2];
    /* draft mode */
    int mode;                               /* DW_MODE_CARD (random) or DW_MODE_DRAFT */
    int pick_no, offer[2], left[3];         /* current offer and bucket counts remaining (1-of / 2-of / 3-of) */
    int npicks, pk_card[DW_DRAFT_PICKS], pk_mult[DW_DRAFT_PICKS], pend_card, pend_mult;
    int deck_n, deck_id; int8_t deck[DW_DRAFT_DECK]; int dumped_draft;
    /* Draft Hub (S510) -- "Burned Proxies"/loss cap, ticket cash-out on run end. hub_* mirrors
     * IDUNA's own game_draft_runs row for the player (dwi_draft_run_state), refreshed whenever the
     * Hub is (re)entered. resume_pending marks that the NEXT connection should submit hub_deck via
     * DW_C_DRAFT_RESUME instead of auto-queueing/redrafting (see do_connect/handle_msg). */
    int hub_active, hub_wins, hub_losses, hub_deck_n; int8_t hub_deck[DW_DRAFT_DECK];
    char hub_msg[64]; int resume_pending, awaiting_resume_queue;
    int name_from_cli;   /* S512: only a --name dev override bypasses server-side lore-name auto-generation */
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

/* ---------- IDUNA zero-friction auth (S508) ---------- */
static void refresh_tickets(void) { if (A.player_id[0]) dwi_ticket_balance(&A.idu, A.player_id, &A.tickets); }
static void iduna_bootstrap(void) {
    if (A.token[0]) return;   /* explicit --token/DW_TOKEN overrides auto-auth, e.g. for --no-auth server testing */
    if (dwi_configure(&A.idu, A.iduna_url, NULL, NULL) != 0) { snprintf(A.err, sizeof A.err, "Bad --iduna-url"); return; }
    char pid[48] = "", secret[72] = "", tok[DW_MAX_AUTH_TOKEN + 1] = "", gotname[DW_NAME_LEN + 1] = "";
    FILE *f = fopen(A.account_path, "r");
    if (f) { if (fscanf(f, "%47s %71s", pid, secret) != 2) { pid[0] = 0; secret[0] = 0; } fclose(f); }
    int loginStatus = 0;
    int ok = pid[0] && secret[0] && dwi_guest_login(&A.idu, pid, secret, tok, sizeof tok, gotname, sizeof gotname, &A.tickets, &loginStatus) == 0;
    int regStatus = 0;
    if (!ok) {
        /* S512 zero-friction auth: NEVER a client-chosen name -- an empty display_name tells
         * IDUNA to auto-assign a lore-friendly one ("Runner-A7B2"), read back into gotname below.
         * "Claim Account" (do_link_email) is where a player later picks something of their own. */
        char newpid[48], newsecret[72];
        const char *wantname = A.name_from_cli ? A.name : "";   /* dev override only -- see --name */
        if (dwi_guest_register(&A.idu, wantname, newpid, sizeof newpid, newsecret, sizeof newsecret, tok, sizeof tok, gotname, sizeof gotname, &A.tickets, &regStatus) == 0) {
            snprintf(pid, sizeof pid, "%s", newpid); snprintf(secret, sizeof secret, "%s", newsecret);
            FILE *wf = fopen(A.account_path, "w");
            if (wf) { fprintf(wf, "%s %s\n", pid, secret); fclose(wf); }
            ok = 1;
        }
    }
    if (ok) {
        snprintf(A.token, sizeof A.token, "%s", tok);
        snprintf(A.player_id, sizeof A.player_id, "%s", pid);
        if (gotname[0]) snprintf(A.name, sizeof A.name, "%s", gotname);
        A.auth_ready = 1;
        fprintf(stderr, "dw_gui: IDUNA auth ok, player_id=%s name=%s tickets=%d\n", A.player_id, A.name, A.tickets);
    } else {
        /* S517: found-live bug -- every auth failure (network down, wrong URL, AND a plain HTTP
         * rejection like a 429 rate limit) was collapsed into "IDUNA offline", which is actively
         * misleading when IDUNA is up and correctly enforcing a real rule (e.g. the 3-new-
         * accounts-per-IP-per-day signup cap). Surface the real status so this is diagnosable
         * without a server-side log dive next time. */
        if (regStatus == 429) {
            snprintf(A.err, sizeof A.err, "Too many new accounts from this network today -- try again tomorrow");
        } else if (regStatus > 0) {
            snprintf(A.err, sizeof A.err, "IDUNA rejected account setup (HTTP %d)", regStatus);
        } else if (loginStatus > 0 && loginStatus != 404) {
            snprintf(A.err, sizeof A.err, "IDUNA rejected saved account (HTTP %d) -- delete dw_account.txt to get a new one", loginStatus);
        } else {
            snprintf(A.err, sizeof A.err, "IDUNA unreachable -- playing without an account (no tickets)");
        }
        fprintf(stderr, "dw_gui: IDUNA auth failed against %s (login_status=%d register_status=%d)\n", A.iduna_url, loginStatus, regStatus);
    }
}
static void do_redeem(void) {
    if (!A.redeem_code[0]) return;
    if (!A.auth_ready) { snprintf(A.redeem_msg, sizeof A.redeem_msg, "No account (IDUNA offline)"); return; }
    int granted = 0, founder = 0, balance = 0;
    int rc = dwi_redeem(&A.idu, A.token, A.redeem_code, &granted, &founder, &balance);
    if (rc != 0) { snprintf(A.redeem_msg, sizeof A.redeem_msg, "Invalid or already-used code"); return; }
    A.tickets = balance; if (founder) A.is_founder = 1;
    snprintf(A.redeem_msg, sizeof A.redeem_msg, "+%d ticket%s%s!", granted, granted == 1 ? "" : "s", founder ? " + FOUNDER" : "");
    A.redeem_code[0] = 0;
}
static void do_connect(void);           /* defined below -- start_draft_run/do_resume_uplink need it */
static void requeue(int same_deck);     /* defined below -- do_resume_uplink needs it */

/* ---------- Draft Hub (S510) ---------- */
static void refresh_draft_hub(void) {
    if (!A.auth_ready) return;
    int deckbuf[DW_DRAFT_DECK], deck_n = 0;
    if (dwi_draft_run_state(&A.idu, A.token, &A.hub_active, &A.hub_wins, &A.hub_losses, deckbuf, DW_DRAFT_DECK, &deck_n) != 0) return;
    A.hub_deck_n = deck_n;
    for (int i = 0; i < deck_n; i++) A.hub_deck[i] = (int8_t)deckbuf[i];
}
/* DRAFT button (S510): the real ticket spend now happens HERE, against IDUNA, before any
 * dw_server connection -- previously the "TICKETS: %d > 0" gate was purely a client-side
 * courtesy check with nothing server-side ever actually decrementing a balance, a real
 * unenforced-economy gap found readying this for the Itch.io launch plan. */
static void start_draft_run(void) {
    if (!A.auth_ready) { snprintf(A.err, sizeof A.err, "No account (IDUNA offline)"); return; }
    int wins = 0, losses = 0, spent = 0;
    int rc = dwi_draft_run_start(&A.idu, A.token, &wins, &losses, &spent);
    if (rc == -2) { snprintf(A.err, sizeof A.err, "Insufficient tickets"); refresh_tickets(); return; }
    if (rc != 0) { snprintf(A.err, sizeof A.err, "IDUNA unreachable"); return; }
    refresh_tickets();
    if (!spent) {   /* resuming an already-active run -- straight to the Hub, never re-spend */
        A.hub_wins = wins; A.hub_losses = losses;
        refresh_draft_hub();
        A.hub_active = 1; A.screen = S_DRAFT_HUB; A.hub_msg[0] = 0;
        return;
    }
    A.mode = DW_MODE_DRAFT; do_connect();   /* fresh run: draft as usual, deck saved to IDUNA on DW_S_DRAFT_DONE */
}
static void do_draft_abort(void) {
    if (!A.auth_ready) return;
    int wins = 0, losses = 0, granted = 0, balance = 0;
    if (dwi_draft_run_abort(&A.idu, A.token, &wins, &losses, &granted, &balance) != 0) {
        snprintf(A.hub_msg, sizeof A.hub_msg, "Abort failed (IDUNA unreachable)");
        return;
    }
    A.tickets = balance; A.hub_active = 0;
    if (A.connected) { send_simple(DW_C_LEAVE); dwc_close(&A.c); A.connected = 0; }   /* Hub reached mid-run (still connected from S_END) */
    snprintf(A.redeem_msg, sizeof A.redeem_msg, "Extracted: %d win%s -> +%d ticket%s", wins, wins == 1 ? "" : "s", granted, granted == 1 ? "" : "s");
    A.link_msg[0] = 0; A.err[0] = 0;
    A.screen = S_MENU;
}
/* RESUME UPLINK (S510): if the Hub was reached mid-run (still connected, coming from S_END),
 * dw_server already holds this deck in memory -- just requeue. Otherwise (boot-time resume, no
 * connection) reconnect and submit the persisted deck via DW_C_DRAFT_RESUME (see handle_msg's
 * DW_S_WELCOME case), then QUEUE once the server echoes it back. */
static void do_resume_uplink(void) {
    if (A.connected) { requeue(1); return; }
    A.mode = DW_MODE_DRAFT;
    memcpy(A.deck, A.hub_deck, DW_DRAFT_DECK); A.deck_n = A.hub_deck_n; A.deck_id = 0;
    A.resume_pending = 1;
    do_connect();
}
static void do_link_email(void) {
    if (!A.link_email[0] || !A.link_pass[0]) return;
    if (!A.auth_ready) { snprintf(A.link_msg, sizeof A.link_msg, "No account (IDUNA offline)"); return; }
    if (strlen(A.link_pass) < 8) { snprintf(A.link_msg, sizeof A.link_msg, "Password needs 8+ characters"); return; }
    char tok[DW_MAX_AUTH_TOKEN + 1];
    int rc = dwi_guest_upgrade(&A.idu, A.token, A.link_email, A.link_pass, tok, sizeof tok);
    if (rc != 0) { snprintf(A.link_msg, sizeof A.link_msg, "Link failed (email taken or bad login)"); return; }
    snprintf(A.token, sizeof A.token, "%s", tok);
    snprintf(A.link_msg, sizeof A.link_msg, "Linked! Progress now saved.");
    A.link_email[0] = 0; A.link_pass[0] = 0;
}

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
    A.screen = S_QUEUE; A.waiting = 0; A.nlog = 0; A.have_reveal = 0; A.state_at = SDL_GetTicks(); A.npicks = 0;
    if (!A.resume_pending) A.deck_n = 0;   /* resume: keep the persisted deck we just staged in A.deck */
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

/* ---------- round-resolution animation plumbing ---------- */
static void fx_targets(int snap) {
    float h[2] = {(float)(A.hull_you < 0 ? 0 : A.hull_you), (float)(A.hull_opp < 0 ? 0 : A.hull_opp)};
    float ar[2] = {(float)A.armor_you, A.armor_opp == DW_HIDDEN_U8 ? 0.0f : (float)A.armor_opp};
    float en[2] = {(float)A.energy_you, A.energy_opp == DW_HIDDEN_U8 ? 0.0f : (float)A.energy_opp};
    float v[2] = {(float)A.vault_you, A.vault_opp == DW_HIDDEN_I8 ? 0.0f : (float)A.vault_opp};
    fx_set_meters(h, ar, en, v, snap);
}
static int fx_has_channel(int id, int ch) { return id >= 0 && (fx_ch(card_fx_a(id)) == ch || fx_ch(card_fx_b(id)) == ch); }
/* energy effect estimate: what the next ROUND_START energy is vs what paying/banking + the +2 regen alone would give */
static int fx_energy_estimate(int seat, int energy_next, int *capped) {
    *capped = 0;
    if (energy_next == DW_HIDDEN_U8 || A.rs_energy[seat] == DW_HIDDEN_U8) return FX_UNKNOWN;
    int card = seat == 0 ? A.pend.card[0] : A.pend.card[1];
    int base = card < 0 ? (A.rs_energy[seat] + 1 > 6 ? 6 : A.rs_energy[seat] + 1) : A.rs_energy[seat] - card_cost(card);
    int expect = base + 2 > 6 ? 6 : base + 2;
    int d = energy_next - expect;
    if (energy_next >= 6 && expect >= 6 && fx_has_channel(A.pend.eff[seat] >= 0 ? A.pend.eff[seat] : card, 23)) *capped = 1;
    return d;
}
static void fx_finish_round(int have_next, const DwMsg *next) {
    if (!A.pend_valid) return;
    FxRound *r = &A.pend;
    for (int s = 0; s < 2; s++) { r->energy_delta[s] = FX_UNKNOWN; r->energy_capped[s] = 0; r->status_after[s] = r->status_before[s]; }
    r->lock_after = 0;
    if (have_next) {
        int en[2] = {next->u.round_start.energy_you, next->u.round_start.energy_opp};
        for (int s = 0; s < 2; s++) r->energy_delta[s] = fx_energy_estimate(s, en[s], &r->energy_capped[s]);
        r->status_after[0] = next->u.round_start.status_you; r->status_after[1] = next->u.round_start.status_opp; r->lock_after = next->u.round_start.lock_mask;
    }
    fx_begin(r);
    A.pend_valid = 0;
}

static void handle_msg(const DwMsg *m) {
    switch (m->type) {
    case DW_S_WELCOME:
        A.welcomed = 1;
        if (A.resume_pending) {
            DwMsg rm; memset(&rm, 0, sizeof rm); rm.type = DW_C_DRAFT_RESUME; memcpy(rm.u.draft_resume.cards, A.deck, DW_DRAFT_DECK);
            A.resume_pending = 0; A.awaiting_resume_queue = 1;
            if (dwc_send(&A.c, &rm)) to_menu("Connection lost");
        } else send_simple(DW_C_QUEUE);
        break;
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
        if (A.awaiting_resume_queue) {
            /* DRAFT_RESUME echo, not a fresh draft -- dw_server doesn't auto-queue this path
             * (see apps/server/main.c's DW_C_DRAFT_RESUME handler), so explicitly do it now. */
            A.awaiting_resume_queue = 0;
            DwMsg qm; memset(&qm, 0, sizeof qm); qm.type = DW_C_QUEUE; qm.u.queue.same_deck = 1;
            if (dwc_send(&A.c, &qm)) to_menu("Connection lost");
        } else if (A.auth_ready) {
            /* A genuinely new deck -- persist it to IDUNA so a boot-time resume (Draft Hub) and
             * the leaderboard have a real server-side record, not just what this one connection
             * holds in memory. Fire-and-forget: a save failure here doesn't block play, matching
             * every other "IDUNA outage never blocks the match" contract in this client. */
            int deckbuf[DW_DRAFT_DECK]; for (int i = 0; i < DW_DRAFT_DECK; i++) deckbuf[i] = A.deck[i];
            dwi_draft_run_save_deck(&A.idu, A.token, deckbuf, DW_DRAFT_DECK);
        }
        break;
    case DW_S_MATCH_FOUND:
        A.match_id = m->u.match_found.match_id; A.seat = m->u.match_found.seat; A.opp_kind = m->u.match_found.opp_kind;
        snprintf(A.opp, sizeof A.opp, "%s", m->u.match_found.opp_name);
        A.screen = S_MATCH; A.nlog = 0; A.have_reveal = 0; A.round = 0; A.sel = -2; A.locked = 0;
        A.hull_you = A.hull_opp = start_hull(); A.armor_you = A.armor_opp = 0; A.vault_you = A.vault_opp = start_vault(); A.lock_mask = 0;
        A.state_at = SDL_GetTicks(); A.dumped_mid = 0;
        A.pend_valid = 0; A.end_wait = 0; memset(A.status, 0, sizeof A.status);
        for (int i = 0; i < 2; i++) { A.rs_hull[i] = start_hull(); A.rs_armor[i] = 0; A.rs_vault[i] = start_vault(); A.rs_energy[i] = start_energy(); }
        fx_reset(); fx_targets(1);
        break;
    case DW_S_ROUND_START:
        A.round = m->u.round_start.round; A.hull_you = m->u.round_start.hull_you; A.hull_opp = m->u.round_start.hull_opp;
        A.energy_you = m->u.round_start.energy_you; A.energy_opp = m->u.round_start.energy_opp;
        memcpy(A.hand, m->u.round_start.hand, 4); A.opp_hand = m->u.round_start.opp_hand_size;
        A.armor_you = m->u.round_start.armor_you; A.armor_opp = m->u.round_start.armor_opp;
        A.vault_you = m->u.round_start.vault_you; A.vault_opp = m->u.round_start.vault_opp; A.lock_mask = m->u.round_start.lock_mask;
        A.deadline_at = m->u.round_start.deadline_ms ? SDL_GetTicks() + m->u.round_start.deadline_ms : 0;
        A.sel = -2; A.locked = 0; A.state_at = SDL_GetTicks();
        fx_finish_round(1, m);                                   /* the previous round's animation, now that its energy/status outcome is known */
        A.status[0] = m->u.round_start.status_you; A.status[1] = m->u.round_start.status_opp;
        fx_targets(!fx_active());
        fx_status_changed(A.status[0], A.status[1], A.lock_mask, 1);
        fx_set_redline(A.hull_you, A.hull_opp);
        A.rs_hull[0] = A.hull_you; A.rs_hull[1] = A.hull_opp; A.rs_armor[0] = A.armor_you; A.rs_armor[1] = A.armor_opp;
        A.rs_vault[0] = A.vault_you; A.rs_vault[1] = A.vault_opp; A.rs_energy[0] = A.energy_you; A.rs_energy[1] = A.energy_opp;
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
        {   /* remember everything the animation needs; it starts at the next ROUND_START (energy/status outcomes) or at MATCH_END */
            if (A.pend_valid) fx_finish_round(0, NULL);
            FxRound *r = &A.pend; memset(r, 0, sizeof *r); r->round = m->u.round_result.round;
            r->card[0] = m->u.round_result.card_you; r->card[1] = m->u.round_result.card_opp; r->eff[0] = m->u.round_result.eff_you; r->eff[1] = m->u.round_result.eff_opp;
            r->dmg[0] = m->u.round_result.dmg_you; r->dmg[1] = m->u.round_result.dmg_opp; r->heal[0] = m->u.round_result.heal_you; r->heal[1] = m->u.round_result.heal_opp;
            r->hull_before[0] = A.rs_hull[0]; r->hull_before[1] = A.rs_hull[1]; r->hull_after[0] = m->u.round_result.hull_you; r->hull_after[1] = m->u.round_result.hull_opp;
            r->armor_before[0] = A.rs_armor[0]; r->armor_before[1] = A.rs_armor[1] == DW_HIDDEN_U8 ? 0 : A.rs_armor[1];
            r->armor_after[0] = m->u.round_result.armor_you; r->armor_after[1] = m->u.round_result.armor_opp == DW_HIDDEN_U8 ? r->armor_before[1] : m->u.round_result.armor_opp;
            r->vault_before[0] = A.rs_vault[0]; r->vault_before[1] = A.rs_vault[1]; r->vault_after[0] = m->u.round_result.vault_you; r->vault_after[1] = m->u.round_result.vault_opp;
            r->flags[0] = m->u.round_result.flags_you; r->flags[1] = m->u.round_result.flags_opp;
            r->status_before[0] = A.status[0]; r->status_before[1] = A.status[1];
            r->seed = (A.match_id * 2654435761u) ^ (uint32_t)m->u.round_result.round * 40503u ^ (uint32_t)m->u.round_result.roll_you;
            A.pend_valid = 1;
        }
        break;
    case DW_S_MATCH_END:
        A.result = m->u.match_end.result; A.reason = m->u.match_end.reason; A.state_at = SDL_GetTicks();
        A.dumped_end = 0;
        if (A.pend_valid) { A.pend.lethal = 1; fx_targets(0); fx_finish_round(0, NULL); }
        if (fx_active() && A.screen == S_MATCH) A.end_wait = 1;   /* let the final clash land before the result screen */
        else A.screen = S_END;
        break;
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
    rect(x, y, w, h, f);
    /* Found live (S513, "spin up the client and play it"): a long label at the usual scale 3
     * ("PRACTICE  (RANDOM DECK, FREE)") overflows the button and reads as truncated on the left.
     * Drop to scale 2 whenever it wouldn't fit at 3, rather than letting any future long label
     * silently repeat this. */
    int scale = text_w(3, label) <= w - 8 ? 3 : 2;
    text_c(x + w / 2, y + h / 2 - (scale == 3 ? 10 : 7), scale, enabled ? C_TEXT : C_DIM, "%s", label);
}
/* Meters draw the animated ("shown") values, which lag the server's until the resource stage of the round animation. */
static void hull_bar(int y, float hull, const char *label, float armor, float vault, int hidden, int seat) {
    float hp = hull < 0 ? 0 : hull; int mx = start_hull();
    rect(20, y, 440, 26, C_PANEL);
    rect(20, y, (int)(440 * (hp > mx ? mx : hp) / mx + 0.5f), 26, hp * 3 <= mx ? C_BAD : C_GOOD);
    float pl = fx_pulse(seat, 0); if (pl > 0) frame(19, y - 1, 442, 28, C_BAD, 2);
    text(28, y + 5, 2, C_TEXT, "%s %d/%d", label, (int)(hp + 0.5f), mx);
    Col ca = fx_pulse(seat, 1) > 0 ? (Col){200, 210, 225} : C_TEXT, cv = fx_pulse(seat, 3) > 0 ? (Col){255, 215, 80} : C_TEXT;
    if (hidden) text(300, y + 5, 2, C_TEXT, "A? $?");
    else { text(300, y + 5, 2, ca, "A%d", (int)(armor + 0.5f)); text(300 + 6 * 2 * 4, y + 5, 2, cv, "$%d", (int)(vault + (vault < 0 ? -0.5f : 0.5f))); }
    fx_draw_status_panel(seat, 20, y, 440, 26);
}
static void pips(int y, float energy, const char *label, int hidden, int seat) {
    text(20, y + 2, 2, C_DIM, "%s", label);
    if (hidden) { text(112, y + 2, 2, C_DIM, "? (HIDDEN)"); return; }
    int grow = fx_pulse(seat, 2) > 0 ? 2 : 0;
    for (int i = 0; i < 6; i++) {
        float f = energy - i; Col c = f >= 1 ? (Col){250, 210, 70} : f > 0.5f ? (Col){200, 170, 60} : C_PANEL;
        rect(112 + i * 34 - grow, y - grow, 28 + 2 * grow, 16 + 2 * grow, c);
    }
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
    else { int mc = (w - 4) / 6; text_c(x + w / 2, y + 12, 1, C_TEXT, "%.*s", mc, nm); }   /* long names are cut to the card width */
    int ty;
    if (big) {
        text_c(x + w / 2, y + 36, 2, tc, "COST %d  PWR %d", card_cost(id), card_power(id));
        text_c(x + w / 2, y + 54, 1, C_DIM, "%s%s%s%s", KIND_NAME[card_kind(id)], card_keyword(id) ? " / " : "", dw_keyword_name(card_keyword(id)), card_credit(id) ? " $" : "");
        ty = y + 68;
    } else {
        text_c(x + w / 2, y + 36, 1, tc, "COST %d  PWR %d", card_cost(id), card_power(id));
        text_c(x + w / 2, y + 48, 1, C_DIM, "%s%s", card_keyword(id) ? dw_keyword_name(card_keyword(id)) : KIND_NAME[card_kind(id)], card_credit(id) ? " $" : "");   /* small cards: keyword (Operations) or kind */
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
/* S512: cosmetic display cap only, matching tier_alpha's real server-side cap (IDUNA/internal/
 * http/handlers/game_online.go's own tierCaps) -- the client has no "what tier am I" response
 * field to read this from yet, so it's hardcoded rather than invented client-side state. */
#define DW_GUEST_TICKET_CAP 20
static void draw_boot(void) {
    text_c(W / 2, 90, 5, C_TEXT, "DEADWEIGHT");
    text_c(W / 2, 420, 2, C_DIM, "ESTABLISHING CONNECTION...");
}
static void draw_menu(int mx, int my) {
    text_c(W / 2, 70, 5, C_TEXT, "DEADWEIGHT");
    /* S512 zero-friction auth: no NAME/HOST/PORT boxes -- players never see an IP or port, and
     * never pick a username on boot. The server auto-assigns a lore-friendly name (see
     * iduna_bootstrap); a real one is picked later via CLAIM ACCOUNT below. */
    if (A.auth_ready) {
        text_c(W / 2, 130, 3, C_TEXT, "%s", A.name);
        text_c(W / 2, 168, 2, C_DIM, "TICKETS: %d/%d", A.tickets, DW_GUEST_TICKET_CAP);
        if (A.is_founder) text_c(W / 2, 194, 1, (Col){255, 200, 60}, "FOUNDER");
    } else text_c(W / 2, 140, 2, C_DIM, "NO ACCOUNT (IDUNA OFFLINE)");
    text_c(W / 2, 230, 2, C_DIM, "%s", A.mode == DW_MODE_DRAFT ? "DRAFT MODE" : "CARD MODE  (RANDOM)");
    int can_draft = A.tickets > 0;
    button(40, 266, 400, 60, can_draft ? "DRAFT  (COST: 1 TICKET)" : "DRAFT  (NO TICKETS)", (Col){225, 160, 40}, can_draft, mx, my);
    button(40, 338, 400, 60, "PRACTICE  (RANDOM DECK, FREE)", C_GOOD, 1, mx, my);
    text(40, 416, 2, C_DIM, "REDEEM CODE");
    rect(40, 438, 280, 46, C_PANEL); frame(40, 438, 280, 46, A.focus == 0 ? C_SEL : C_LOCK, 2);
    text(52, 451, 2, C_TEXT, "%s%s", A.redeem_code, (A.focus == 0 && (SDL_GetTicks() / 500) % 2) ? "_" : "");
    button(328, 438, 112, 46, "REDEEM", (Col){70, 110, 200}, A.redeem_code[0] != 0, mx, my);
    text(40, 500, 2, C_DIM, "CLAIM ACCOUNT (LINK EMAIL, KEEP YOUR PROGRESS)");
    rect(40, 522, 400, 40, C_PANEL); frame(40, 522, 400, 40, A.focus == 1 ? C_SEL : C_LOCK, 2);
    text(50, 533, 2, C_TEXT, "%s%s", A.link_email, (A.focus == 1 && (SDL_GetTicks() / 500) % 2) ? "_" : "");
    rect(40, 566, 400, 40, C_PANEL); frame(40, 566, 400, 40, A.focus == 2 ? C_SEL : C_LOCK, 2);
    { char mask[sizeof A.link_pass]; size_t n = strlen(A.link_pass); for (size_t i = 0; i < n; i++) mask[i] = '*'; mask[n] = 0;
      text(50, 577, 2, C_TEXT, "%s%s", mask, (A.focus == 2 && (SDL_GetTicks() / 500) % 2) ? "_" : ""); }
    button(40, 610, 400, 40, "CLAIM ACCOUNT", (Col){70, 110, 200}, A.link_email[0] && A.link_pass[0], mx, my);
    if (A.link_msg[0]) text_c(W / 2, 670, 1, C_GOOD, "%s", A.link_msg);
    else if (A.redeem_msg[0]) text_c(W / 2, 670, 1, C_GOOD, "%s", A.redeem_msg);
    else if (A.err[0]) text_c(W / 2, 670, 1, C_BAD, "%s", A.err);
    text_c(W / 2, 940, 1, C_DIM, "TAB = NEXT FIELD   ESC = QUIT   V%s", DW_VERSION);
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
static void draw_draft_hub(int mx, int my) {
    text_c(W / 2, 50, 4, C_TEXT, "DRAFT HUB");
    text_c(W / 2, 110, 3, C_TEXT, "WINS: %d", A.hub_wins);
    text_c(W / 2, 148, 1, C_DIM, "BURNED PROXIES");
    for (int i = 0; i < 3; i++) {
        int bx = W / 2 - 84 + i * 64;
        rect(bx, 166, 48, 48, i < A.hub_losses ? C_BAD : C_PANEL);
        frame(bx, 166, 48, 48, C_TEXT, 2);
    }
    text(24, 244, 2, C_DIM, "DECK  (%d CARDS)", A.hub_deck_n);
    int rows = A.hub_deck_n < 18 ? A.hub_deck_n : 18;
    for (int i = 0; i < rows; i++) text(24, 272 + i * 20, 1, C_TEXT, "%s", dw_card_name(A.hub_deck[i]));
    button(40, 690, 400, 64, "RESUME UPLINK", C_GOOD, 1, mx, my);
    button(40, 766, 400, 56, "ABORT & EXTRACT", C_BAD, 1, mx, my);
    if (A.hub_msg[0]) text_c(W / 2, 838, 1, C_BAD, "%s", A.hub_msg);
    text_c(W / 2, 940, 1, C_DIM, "V%s", DW_VERSION);
}
static void draw_queue(int mx, int my) {
    text_c(W / 2, 250, 4, C_TEXT, "SEARCHING...");
    text_c(W / 2, 320, 2, C_DIM, A.welcomed ? "IN QUEUE  (%d WAITING)" : "CONNECTING...", A.waiting);
    text_c(W / 2, 360, 2, C_DIM, "PLAYING AS %s", A.name);
    button(140, 520, 200, 64, "CANCEL", C_BAD, 1, mx, my);
}
static void draw_match(int mx, int my) {
    FxShown sh = fx_shown();
    int dx = 0, dy = 0; fx_get_shake(&dx, &dy);
    SDL_Rect vp = {dx, dy, W, H}; SDL_RenderSetViewport(R, (dx || dy) ? &vp : NULL);
    text(20, 14, 2, C_TEXT, "%s%s", A.opp, A.opp_kind ? " (BOT)" : "");
    text(330, 14, 1, C_DIM, "M = SOUND %s", sfx_is_muted() ? "OFF" : sfx_is_open() ? "ON" : "N/A");
    hull_bar(44, sh.hull[1], "HULL", sh.armor[1], sh.vault[1], A.armor_opp == DW_HIDDEN_U8, 1);
    pips(80, sh.energy[1], "ENERGY", A.energy_opp == DW_HIDDEN_U8, 1); text(350, 82, 2, C_DIM, "HAND %d", A.opp_hand);
    text_c(W / 2, 118, 3, C_TEXT, "ROUND %d/%d", A.round, max_rounds());
    if (A.deadline_at) { int left = (int)(A.deadline_at - SDL_GetTicks()); if (left < 0) left = 0; text_c(W / 2, 148, 2, left < 5000 ? C_BAD : C_DIM, "%d S", left / 1000); }
    if (fx_active()) {
        fx_draw_arena(A.rv_you, A.rv_opp, A.have_reveal);
    } else if (A.have_reveal) {
        text_c(W / 2, 176, 2, C_DIM, "LAST ROUND (%d)", A.rv_round);
        text_c(120, 200, 2, C_DIM, "YOU"); text_c(360, 200, 2, C_DIM, "OPP");
        card_box(20, 222, 200, 170, A.rv_you, 0, 0); card_box(260, 222, 200, 170, A.rv_opp, 0, 0);
        text_c(120, 398, 2, A.rv_dy ? C_BAD : C_DIM, "TOOK %d", A.rv_dy); text_c(360, 398, 2, A.rv_do ? C_GOOD : C_DIM, "TOOK %d", A.rv_do);
    } else text_c(W / 2, 260, 2, C_DIM, "PICK A CARD OR PASS");
    for (int i = 0; i < A.nlog; i++) text(20, 424 + i * 20, 2, C_DIM, "%s", A.log[i]);
    hull_bar(552, sh.hull[0], "YOU", sh.armor[0], sh.vault[0], 0, 0); pips(588, sh.energy[0], "ENERGY", 0, 0);
    for (int i = 0; i < 4; i++) {
        int st = A.hand[i] < 0 ? 2 : !slot_legal(i) ? 2 : (A.sel == i ? (A.locked ? 3 : 1) : 0);
        int cx = 20 + (i % 2) * 232, cy = 620 + (i / 2) * 142;
        card_box(cx, cy, 216, 136, A.hand[i], i + 1, st);
        if (A.hand[i] >= 0 && ((A.lock_mask >> i) & 1)) fx_draw_disabled_card(cx, cy, 216, 136);   /* EMP: desaturated, scanlined, chained */
    }
    int can = !A.locked && A.sel != -2;
    button(20, 906, 200, 42, A.sel == -1 ? (A.locked ? "PASSED" : "PASS *") : "PASS", C_LOCK, !A.locked, mx, my);
    button(240, 906, 220, 42, A.locked ? "LOCKED" : "LOCK IN", C_GOOD, can, mx, my);
    fx_draw_overlay();
    SDL_RenderSetViewport(R, NULL);
}
static void draw_end(int mx, int my) {
    Col c = A.result == DW_RES_WIN ? C_GOOD : A.result == DW_RES_LOSS ? C_BAD : C_DIM;
    text_c(W / 2, 220, 6, c, "%s", A.result == DW_RES_WIN ? "VICTORY" : A.result == DW_RES_LOSS ? "DEFEAT" : "DRAW");
    const char *why[5] = {"HULL DESTROYED", "ROUNDS OVER", "OPPONENT FORFEIT", "SERVER", "BANKRUPT"};
    text_c(W / 2, 310, 2, C_DIM, "%s", why[A.reason > 4 ? 3 : A.reason]);
    text_c(W / 2, 350, 2, C_TEXT, "YOU %d  -  %s %d", A.hull_you < 0 ? 0 : A.hull_you, A.opp, A.hull_opp < 0 ? 0 : A.hull_opp);
    if (A.mode == DW_MODE_DRAFT && A.deck_n) {
        /* S510: routes through the Draft Hub (wins/losses/Abort & Extract) instead of instantly
         * requeuing -- "players do not queue immediately after drafting, nor when resuming." */
        button(60, 460, 360, 70, "DRAFT HUB", C_GOOD, 1, mx, my);
        text_c(W / 2, 550, 1, C_DIM, "DECK %d", A.deck_id);
    } else button(60, 480, 360, 70, "PLAY AGAIN", C_GOOD, 1, mx, my);
    button(60, 640, 360, 56, "MENU", C_LOCK, 1, mx, my);
}
static void draw(int mx, int my) {
    setc(C_BG, 255); SDL_RenderClear(R);
    switch (A.screen) { case S_BOOT: draw_boot(); break; case S_MENU: draw_menu(mx, my); break; case S_QUEUE: draw_queue(mx, my); break;
                        case S_MATCH: draw_match(mx, my); break; case S_DRAFT: draw_draft(mx, my); break;
                        case S_DRAFT_HUB: draw_draft_hub(mx, my); break; default: draw_end(mx, my); break; }
}

/* ---------- input ---------- */
/* S512: NAME/HOST/PORT are no longer player-editable fields at all (removed with the boxes
 * themselves in draw_menu) -- only the 3 remaining text boxes need focus/typing support. */
static char *field(int i) {
    switch (i) { case 0: return A.redeem_code; case 1: return A.link_email; default: return A.link_pass; }
}
static size_t field_cap(int i) {
    switch (i) { case 0: return sizeof A.redeem_code - 1; case 1: return sizeof A.link_email - 1; default: return sizeof A.link_pass - 1; }
}
#define MENU_FIELDS 3
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
        if (in_rect(x, y, 40, 438, 280, 46)) A.focus = 0;
        if (in_rect(x, y, 40, 522, 400, 40)) A.focus = 1;
        if (in_rect(x, y, 40, 566, 400, 40)) A.focus = 2;
        if (in_rect(x, y, 40, 266, 400, 60) && A.tickets > 0) start_draft_run();
        if (in_rect(x, y, 40, 338, 400, 60)) { A.mode = DW_MODE_CARD; do_connect(); }
        if (in_rect(x, y, 328, 438, 112, 46) && A.redeem_code[0]) do_redeem();
        if (in_rect(x, y, 40, 610, 400, 40) && A.link_email[0] && A.link_pass[0]) do_link_email();
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
            if (in_rect(x, y, 60, 460, 360, 70)) { refresh_draft_hub(); A.hub_active = 1; A.screen = S_DRAFT_HUB; }
        } else if (in_rect(x, y, 60, 480, 360, 70)) requeue(0);
        if (in_rect(x, y, 60, 640, 360, 56)) to_menu("");
        break;
    case S_DRAFT_HUB:
        if (in_rect(x, y, 40, 700, 400, 64)) do_resume_uplink();
        if (in_rect(x, y, 40, 776, 400, 56)) do_draft_abort();
        break;
    }
}
/* Shared by SDL_TEXTINPUT (typing) and Ctrl+V (paste, S508d -- "ensure the text input box fully
 * supports OS-level copy/paste so players do not have to type" the 29-char redeem code by hand). */
static void append_to_field(int focus, const char *text) {
    char *f = field(focus); size_t n = strlen(f);
    for (const char *p = text; *p && n < field_cap(focus); p++)
        if ((unsigned char)*p >= 32 && (unsigned char)*p < 127) { f[n++] = *p; f[n] = 0; }
}
static void key(SDL_Keycode k) {
    if (A.screen == S_MENU) {
        char *f = field(A.focus);
        if (k == SDLK_BACKSPACE && *f) f[strlen(f) - 1] = 0;
        else if (k == SDLK_TAB) A.focus = (A.focus + 1) % MENU_FIELDS;
        else if (k == SDLK_F2) A.mode = A.mode == DW_MODE_DRAFT ? DW_MODE_CARD : DW_MODE_DRAFT;
        else if (k == SDLK_v && (SDL_GetModState() & KMOD_CTRL)) {
            if (SDL_HasClipboardText()) {
                char *clip = SDL_GetClipboardText();
                if (clip) { append_to_field(A.focus, clip); SDL_free(clip); }
            }
        }
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            if (A.focus == 0) { if (A.redeem_code[0]) do_redeem(); }
            else if (A.focus == 1 || A.focus == 2) { if (A.link_email[0] && A.link_pass[0]) do_link_email(); }
        }
    } else if (A.screen == S_MATCH) {
        if (k == SDLK_m) sfx_set_muted(!sfx_is_muted());
        else if (k >= SDLK_1 && k <= SDLK_4) select_slot((int)(k - SDLK_1));
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


/* ---------- fx host callbacks + the offline animation/audio demo (dw_gui --fx-demo DIR) ---------- */
static void fx_text_cb(int x, int y, int scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char *str) { Col c = {r, g, b}; text_a(x, y, scale, c, a, str); }
static int fx_text_w_cb(int scale, const char *str) { return text_w(scale, str); }
static void fx_card_cb(int x, int y, int w, int h, int id, int state) { card_box(x, y, w, h, id, 0, state); }
static void fx_host_init(void) { FxHost h = {R, fx_text_cb, fx_text_w_cb, fx_card_cb}; fx_init(&h); }

typedef struct {
    const char *name; int c0, c1, dmg0, dmg1, heal0, heal1, h0b, h1b, h0a, h1a, a0b, a1b, a0a, a1a, v0b, v1b, v0a, v1a, ed0, ed1, cap0, cap1, fl0, fl1, sb0, sb1, sa0, sa1, lock;
} DemoRound;
/* Offense: Bolt 1, Railgun 2, Spark 0 | Operations: Interceptor 4, Stormwing 5, Piercing Round 25, Sunfire Aegis 40, Corporate Espionage 37,
 * Hush Money 72, Quick Draw 23, Power Tap 83, Glacial Prison 70 | Defense: Barrier 7, Bastion 8, Seraph's Embrace 61, Dividend Stock 44 */
static const DemoRound DEMOS[] = {
    /* name              you opp  dmg     heal   hull b   hull a    armor b   armor a    vault b   vault a  energy  cap   flags  st b  st a  lock */
    {"blitz",             1, 4,   0, 6,   0, 0,  20, 20, 20, 14,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"blitz_crit",        2, 5,   0, 13,  0, 0,  20, 20, 20, 7,   0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"blitz_opp_wins",    4, 1,   6, 0,   0, 0,  20, 20, 14, 20,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"block_absorbed",    7, 1,   0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"block_reflected",   7, 1,   0, 3,   0, 0,  20, 20, 20, 17,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"block_crit",        8, 0,   0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   3, 3,   2, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"bypass_lock",       25, 7,  0, 5,   0, 0,  20, 20, 20, 15,  0, 4,  0, 4,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"bypass_sabotage",   40, 7,  0, 2,   0, 0,  20, 20, 20, 18,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 1, 0},
    {"bypass_flank",      4, 7,   0, 8,   0, 0,  20, 20, 20, 12,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"bypass_scan",       37, 7,  0, 4,   0, 0,  20, 20, 20, 16,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"bypass_siphon",     72, 7,  0, 1,   0, 0,  20, 20, 20, 19,  0, 0,  0, 0,   3, 3,   5, 1,   0, -2, 0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"bypass_crit_flank", 5, 7,   0, 13,  0, 0,  20, 20, 20, 7,   0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"bypass_opp_scan",   7, 37,  4, 0,   0, 0,  20, 20, 16, 20,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"mirror_offense",    1, 1,   6, 6,   0, 0,  20, 20, 14, 14,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"mirror_operations", 4, 4,   3, 3,   0, 0,  20, 20, 17, 17,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"mirror_defense",    7, 7,   0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"unopposed",         1, -1,  0, 3,   0, 0,  20, 20, 20, 17,  0, 0,  0, 0,   3, 3,   3, 3,   1, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"both_pass",        -1, -1,  0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   3, 3,   1, 1,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"armor_both",       61, 61,  0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  4, 4,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"armor_soak",        7, 25,  0, 0,   0, 0,  20, 20, 20, 20,  6, 0,  1, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"credits_gain",     44, -1,  0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   5, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"overload",         43, -1,  0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   3, 3,   2, 0,  1, 0,  0, 0,  0, 0, 0, 0, 0},
    {"burn_ignite",      40, 7,   0, 2,   0, 0,  20, 20, 20, 18,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 1, 0},
    {"emp_disable",      70, 7,   0, 2,   0, 0,  20, 20, 20, 18,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  8, 0,  0, 0, 0, 0, 0},
    {"emp_on_me",         7, 70,  0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 8,  0, 0, 0, 0, 5},
    {"cancelled",        36, 1,   0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  1, 0,  0, 0, 0, 0, 0},
    {"redline",           5, 1,   0, 0,   0, 0,  20, 20, 20, 20,  0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
    {"lethal_blitz",      2, 5,   0, 13,  0, 0,  20, 8,  20, 0,   0, 0,  0, 0,   3, 3,   3, 3,   0, 0,  0, 0,  0, 0,  0, 0, 0, 0, 0},
};
static FxRound demo_round(const DemoRound *d) {
    FxRound r; memset(&r, 0, sizeof r);
    r.round = 5; r.card[0] = d->c0; r.card[1] = d->c1; r.eff[0] = d->c0; r.eff[1] = d->c1;
    if (d->fl0 & 1) r.eff[0] = -1;
    r.dmg[0] = d->dmg0; r.dmg[1] = d->dmg1; r.heal[0] = d->heal0; r.heal[1] = d->heal1;
    r.hull_before[0] = d->h0b; r.hull_before[1] = d->h1b; r.hull_after[0] = d->h0a; r.hull_after[1] = d->h1a;
    r.armor_before[0] = d->a0b; r.armor_before[1] = d->a1b; r.armor_after[0] = d->a0a; r.armor_after[1] = d->a1a;
    r.vault_before[0] = d->v0b; r.vault_before[1] = d->v1b; r.vault_after[0] = d->v0a; r.vault_after[1] = d->v1a;
    r.energy_delta[0] = d->ed0; r.energy_delta[1] = d->ed1; r.energy_capped[0] = d->cap0; r.energy_capped[1] = d->cap1;
    r.flags[0] = d->fl0; r.flags[1] = d->fl1; r.status_before[0] = d->sb0; r.status_before[1] = d->sb1; r.status_after[0] = d->sa0; r.status_after[1] = d->sa1; r.lock_after = d->lock;
    r.seed = 12345u + (uint32_t)d->c0 * 31u + (uint32_t)d->c1; r.lethal = d->h0a <= 0 || d->h1a <= 0;
    return r;
}
static int demo_setup(const DemoRound *d, const FxRound *r) {
    A.screen = S_MATCH; snprintf(A.opp, sizeof A.opp, "BOT-DEMO"); A.opp_kind = 1; A.round = r->round; A.have_reveal = 1;
    A.hand[0] = 1; A.hand[1] = 25; A.hand[2] = 7; A.hand[3] = 40; A.energy_you = 4; A.energy_opp = 4; A.opp_hand = 4;
    A.hull_you = r->hull_after[0]; A.hull_opp = r->hull_after[1]; A.armor_you = r->armor_after[0]; A.armor_opp = r->armor_after[1];
    A.vault_you = r->vault_after[0]; A.vault_opp = r->vault_after[1]; A.lock_mask = d->lock; A.status[0] = r->status_after[0]; A.status[1] = r->status_after[1];
    fx_reset();
    float hb[2] = {(float)r->hull_before[0], (float)r->hull_before[1]}, ab[2] = {(float)r->armor_before[0], (float)r->armor_before[1]}, eb[2] = {4, 4}, vb[2] = {(float)r->vault_before[0], (float)r->vault_before[1]};
    fx_set_meters(hb, ab, eb, vb, 1);
    return 0;
}
static void demo_targets(const FxRound *r) {
    float ha[2] = {(float)(r->hull_after[0] < 0 ? 0 : r->hull_after[0]), (float)(r->hull_after[1] < 0 ? 0 : r->hull_after[1])}, aa[2] = {(float)r->armor_after[0], (float)r->armor_after[1]}, ea[2] = {4.0f + (r->energy_delta[0] == FX_UNKNOWN ? 0 : r->energy_delta[0]), 4.0f + (r->energy_delta[1] == FX_UNKNOWN ? 0 : r->energy_delta[1])}, va[2] = {(float)r->vault_after[0], (float)r->vault_after[1]};
    fx_set_meters(ha, aa, ea, va, 0);
}
static int run_fx_demo(const char *dir) {
    static const int SNAP_MS[] = {250, 700, 1100, 1600, 2100, 2700, 3400, 4100, 4900, 5700};
    int bad = 0; int n = (int)(sizeof DEMOS / sizeof DEMOS[0]);
    printf("fx-demo: %d scenarios\n", n);
    for (int i = 0; i < n; i++) {
        const DemoRound *d = &DEMOS[i]; FxRound r = demo_round(d);
        /* audio: schedule the same cues the animation schedules, render offline, check the bounds */
        demo_setup(d, &r); sfx_offline_begin(); fx_begin(&r); demo_targets(&r);
        float total = fx_total_ms(); int frames = (int)(total / 1000.0f * SFX_RATE) + SFX_RATE; static int16_t big[SFX_RATE * 12 * 2];
        if (frames > SFX_RATE * 12) frames = SFX_RATE * 12;
        sfx_render(big, frames); float pk, rms; int last; sfx_stats(big, frames, 0.004f, &pk, &rms, &last);
        char name[64]; snprintf(name, sizeof name, "%s", fx_scenario_name());
        char path[600]; snprintf(path, sizeof path, "%s/%s.wav", dir, d->name); sfx_write_wav(path, big, last > 0 ? last + SFX_RATE / 10 : 1);
        sfx_offline_end();
        int ok = total <= 9000.0f && pk <= 0.99f && last >= 0;
        printf("  %-20s -> %-26s length %5.0f ms  audio peak %.2f  audio ends %.2f s  %s\n", d->name, name, total, pk, last < 0 ? 0.0f : (float)last / SFX_RATE, ok ? "ok" : "BAD");
        if (!ok) bad++;
        /* visuals: step the clock at 60 fps and dump the arena band at key moments */
        demo_setup(d, &r); fx_set_speed(1); fx_begin(&r); demo_targets(&r);
        if (d->name[0] == 'r' && d->name[1] == 'e') fx_set_redline(3, 10);
        int k = 0; unsigned t = 0;
        while (fx_active() && t < 9500) {
            fx_update(16); t += 16;
            if (k < 10 && t >= (unsigned)SNAP_MS[k] && t < (unsigned)SNAP_MS[k] + 16) {
                setc(C_BG, 255); SDL_RenderClear(R); draw_match(0, 0);
                SDL_Rect rc = {0, 168, 480, 262}; SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, rc.w, rc.h, 32, SDL_PIXELFORMAT_ARGB8888);
                if (sf) { if (SDL_RenderReadPixels(R, &rc, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0) { char p2[600]; snprintf(p2, sizeof p2, "%s/%s_%02d.bmp", dir, d->name, k); SDL_SaveBMP(sf, p2); } SDL_FreeSurface(sf); }
                k++;
            }
        }
        if (fx_active()) { printf("  %s: did not finish\n", d->name); bad++; }
        fx_reset();
    }
    printf("fx-demo: %s\n", bad ? "PROBLEMS" : "ok");
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    snprintf(A.name, sizeof A.name, "Player"); snprintf(A.host, sizeof A.host, "okemily.com"); snprintf(A.port, sizeof A.port, "6980");
    snprintf(A.account_path, sizeof A.account_path, "dw_account.txt");
    const char *iduna_env = getenv("IDUNA_BASE_URL");
    /* S508d, real found-live gap: this default was "http://localhost:8080" -- fine for local dev
     * (matches --no-auth dw_server testing), but a genuine shipping bug for a distributed release
     * build, since a real player's own machine has nothing listening on localhost:8080. The live,
     * public production IDUNA is reachable at https://okemily.com (nginx front door, ops/
     * nginx-front-door-snippet.conf), same host convention A.host's own default (okemily.com)
     * already uses for the game server above -- still overridable via --iduna-url/IDUNA_BASE_URL
     * for local development. */
    snprintf(A.iduna_url, sizeof A.iduna_url, "%s", iduna_env && *iduna_env ? iduna_env : "https://okemily.com");
    A.sel = -2; for (int i = 0; i < 4; i++) A.hand[i] = -1;
    const char *demo_dir = NULL; int no_sound = 0;
    const char *envt = getenv("DW_TOKEN"); if (envt) snprintf(A.token, sizeof A.token, "%s", envt);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--version")) { printf("dw_gui %s\n", DW_VERSION); return 0; }
        else if (!strcmp(a, "--name") && v) { snprintf(A.name, sizeof A.name, "%s", v); A.name_from_cli = 1; i++; }
        else if (!strcmp(a, "--host") && v) { snprintf(A.host, sizeof A.host, "%s", v); i++; }
        else if (!strcmp(a, "--port") && v) { snprintf(A.port, sizeof A.port, "%s", v); i++; }
        else if (!strcmp(a, "--token") && v) { snprintf(A.token, sizeof A.token, "%s", v); i++; }
        else if (!strcmp(a, "--iduna-url") && v) { snprintf(A.iduna_url, sizeof A.iduna_url, "%s", v); i++; }
        else if (!strcmp(a, "--account") && v) { snprintf(A.account_path, sizeof A.account_path, "%s", v); i++; }
        else if (!strcmp(a, "--frames") && v) { A.frames_dir = v; i++; }
        else if (!strcmp(a, "--autostart")) A.autostart = 1;
        else if (!strcmp(a, "--mode") && v) { A.mode = !strcmp(v, "draft") ? DW_MODE_DRAFT : DW_MODE_CARD; i++; }
        else if (!strcmp(a, "--selftest")) A.selftest = A.autostart = 1;
        else if (!strcmp(a, "--fx-demo") && v) { demo_dir = v; i++; }
        else if (!strcmp(a, "--no-sound")) no_sound = 1;
        else { fprintf(stderr, "usage: dw_gui [--name N] [--host H] [--port P] [--token JWT] [--iduna-url URL] [--account FILE] [--autostart] [--selftest [--frames DIR]]\n"
                               "  (--name/--host/--port are developer overrides only -- the release UI never shows these to a player, see S512)\n"); return 2; }
    }
    if (A.selftest || demo_dir) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    dw_net_init();
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("DEADWEIGHT", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    R = SDL_CreateRenderer(win, -1, A.selftest ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R) R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!R) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(R, W, H);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_StartTextInput();
    fx_host_init();
    if (demo_dir) { int rc2 = run_fx_demo(demo_dir); SDL_DestroyRenderer(R); SDL_DestroyWindow(win); SDL_Quit(); return rc2; }
    if (A.selftest) fx_set_speed(40.0f);                    /* headless: play the animations ~40x so a full match still finishes in seconds */
    else if (!no_sound && sfx_open() != 0) fprintf(stderr, "dw_gui: no audio device; continuing silent\n");

    /* S512 boot flow: "Launch Client -> Show 'Establishing Connection...' loading text ->
     * Auto-connect -> Main Menu." A real rendered frame, not just a comment -- the window and
     * renderer now exist BEFORE the blocking IDUNA bootstrap call runs (they used to be created
     * AFTER it, so there was physically no window yet to paint a loading screen into). selftest/
     * fx-demo skip this entirely (see iduna_bootstrap's own doc comment: pure game-mechanics
     * harnesses, network-free by design). */
    if (!A.selftest) {
        A.screen = S_BOOT;
        draw(0, 0); SDL_RenderPresent(R); SDL_PumpEvents();
        iduna_bootstrap();
        /* Boot-time Draft Hub resume (S510): "if draft_active == true, route to
         * SCREEN_DRAFT_HUB, not the draft picker or the queue." */
        refresh_draft_hub();
        A.screen = A.hub_active ? S_DRAFT_HUB : S_MENU;
    } else A.screen = S_MENU;

    if (A.autostart) do_connect();
    if (A.selftest && A.screen != S_QUEUE) { fprintf(stderr, "selftest: %s\n", A.err); return 1; }

    int running = 1, mx = 0, my = 0, rc = 0; Uint32 t0 = SDL_GetTicks(), tprev = t0;
    while (running) {
        Uint32 tnow = SDL_GetTicks(); unsigned dtms = tnow - tprev > 100 ? 100 : tnow - tprev; tprev = tnow;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_MOUSEMOTION) { mx = e.motion.x; my = e.motion.y; }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) click(e.button.x, e.button.y);
            else if (e.type == SDL_TEXTINPUT && A.screen == S_MENU) append_to_field(A.focus, e.text.text);
            else if (e.type == SDL_KEYDOWN) { if (e.key.keysym.sym == SDLK_ESCAPE && A.screen == S_MENU) running = 0; else key(e.key.keysym.sym); }
        }
        pump();
        fx_update(dtms);
        if (A.end_wait && !fx_active()) { A.end_wait = 0; A.screen = S_END; A.state_at = SDL_GetTicks(); }
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
    sfx_close();
    SDL_DestroyRenderer(R); SDL_DestroyWindow(win); SDL_Quit();
    return rc;
}
