/* dw_server -- DEADWEIGHT authoritative match server (S503-04). Single poll() loop hosts the queue and many
 * concurrent card-mode matches over TCP (docs/WIRE_PROTOCOL.md). No blocking calls in the loop. */
#include "net.h"
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include "card_rules.h"
#include "card_text.h"
#include "draft.h"
#include "iduna.h"
#include "match.h"
#include "protocol.h"
#include "version.h"
#ifndef _WIN32
#include <pthread.h>
#define DW_HAVE_WORKER 1
#endif

#define DW_MAX_PLAYS 128          /* per-match play log; must be >= max_rounds() */
#define MAX_CONNS 512
#define MAX_MATCHES 256
#define INBUF 2048
#define OUTBUF 8192
#define ROUND_MS_DEFAULT 40000
#define HELLO_TIMEOUT_MS 10000

enum { S_FREE = 0, S_CONNECTED, S_READY, S_QUEUED, S_IN_MATCH, S_NEEDAUTH, S_VERIFYING, S_DRAFTING };

typedef struct {
    dw_sock fd; int state; uint32_t session_id;
    uint8_t in[INBUF]; size_t in_n;
    uint8_t out[OUTBUF]; size_t out_n;
    char name[DW_NAME_LEN + 1]; uint8_t kind, mode; char player_id[48];
    char match_token[33];                   /* S537 Duel Phase 2: set from QUEUE, cleared to "" when absent */
    int match, seat; uint64_t queued_seq, connect_ms; int close_after_flush;
    DwDraft draft;                          /* draft mode: the in-progress draft */
    int8_t deck[DW_DRAFT_DECK]; int deck_n; uint32_t deck_id;   /* draft mode: the last completed deck (replayable via QUEUE same_deck) */
} Conn;

typedef struct {
    int active; uint32_t id; DwMatch m; int conn[2];
    uint64_t deadline; int8_t plays[DW_MAX_PLAYS][2]; int nplays;
    int mode; uint32_t deck_id[2]; int8_t deck[2][DW_DRAFT_DECK];
} Match;

static Conn conns[MAX_CONNS];
static Match matches[MAX_MATCHES];
static volatile sig_atomic_t stop_flag = 0;
static int opt_ff = 0, opt_noauth = 1, opt_verbose = 0;
static long opt_max_matches = -1;
static int opt_iduna = 0, opt_fail_open = 0, opt_noauth_explicit = 0;
static DwIduna iduna;
static int opt_round_ms = ROUND_MS_DEFAULT;
static const char *opt_log_dir = NULL;
static uint32_t next_session = 1, next_match = 1, next_deck = 1, seed_state = 0x9E3779B9u;
static uint64_t queue_seq = 0;
static long st_drafts = 0, st_draft_matches = 0, st_matches = 0, st_bot_bot = 0, st_human_bot = 0, st_human_human = 0, st_forfeits = 0;

/* ---- IDUNA worker thread: verify + match-result HTTP never runs on the poll() loop ---- */
enum { J_VERIFY = 1, J_REPORT };
typedef struct { int type; int conn; uint32_t gen; char token[DW_MAX_AUTH_TOKEN + 1]; char name[DW_NAME_LEN + 1]; DwMatchReport rep; } Job;
typedef struct { int conn; uint32_t gen; int rc; DwIdentity id; } VResult;
#ifdef DW_HAVE_WORKER
#define JQ 256
static Job jq[JQ]; static int jq_n = 0, jq_head = 0;
static VResult rq[JQ]; static int rq_n = 0;
static pthread_mutex_t jm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t jc = PTHREAD_COND_INITIALIZER;
static pthread_t worker_tid; static int worker_started = 0, worker_stop = 0, wake_fd[2] = { -1, -1 };

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&jm);
        while (jq_n == 0 && !worker_stop) pthread_cond_wait(&jc, &jm);
        if (jq_n == 0 && worker_stop) { pthread_mutex_unlock(&jm); return NULL; }
        Job *j = malloc(sizeof *j);
        if (!j) { pthread_mutex_unlock(&jm); return NULL; }
        *j = jq[jq_head]; jq_head = (jq_head + 1) % JQ; jq_n--;
        pthread_mutex_unlock(&jm);
        if (j->type == J_VERIFY) {
            VResult r; memset(&r, 0, sizeof r); r.conn = j->conn; r.gen = j->gen;
            r.rc = dwi_verify(&iduna, j->token, j->name, &r.id);
            pthread_mutex_lock(&jm);
            if (rq_n < JQ) rq[rq_n++] = r;
            pthread_mutex_unlock(&jm);
            if (write(wake_fd[1], "x", 1) < 0) { /* wake pipe full: the loop is already awake */ }
        } else {
            if (dwi_report(&iduna, &j->rep) != 0)
                fprintf(stderr, "dw_server: IDUNA match-result report failed for match %u (see match log; continuing)\n", j->rep.match_id);
        }
        free(j);
    }
}
static int enqueue_job(const Job *j) {
    int ok = 0;
    pthread_mutex_lock(&jm);
    if (jq_n < JQ) { jq[(jq_head + jq_n) % JQ] = *j; jq_n++; ok = 1; pthread_cond_signal(&jc); }
    pthread_mutex_unlock(&jm);
    return ok;
}
static int start_worker(void) {
    if (pipe(wake_fd) != 0) return -1;
    dw_nonblock(wake_fd[0]); dw_nonblock(wake_fd[1]);
    if (pthread_create(&worker_tid, NULL, worker_main, NULL) != 0) return -1;
    worker_started = 1; return 0;
}
static void stop_worker(void) {
    if (!worker_started) return;
    pthread_mutex_lock(&jm); worker_stop = 1; pthread_cond_signal(&jc); pthread_mutex_unlock(&jm);
    pthread_join(worker_tid, NULL); worker_started = 0;
}
#else
static int enqueue_job(const Job *j) { (void)j; return 0; }
static int start_worker(void) { return -1; }
static void stop_worker(void) {}
#endif

static void on_signal(int s) { (void)s; stop_flag = 1; }
static void vlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void vlog(const char *fmt, ...) {
    if (!opt_verbose) return;
    va_list ap; va_start(ap, fmt); fprintf(stderr, "dw_server: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap);
}

static uint32_t next_seed(void) {
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 17; seed_state ^= seed_state << 5;
    return seed_state ^ (uint32_t)dw_now_ms();
}

static void conn_close(int ci);

static void flush_conn(int ci) {
    Conn *c = &conns[ci];
    while (c->out_n > 0) {
        long n = dw_send(c->fd, c->out, c->out_n);
        if (n > 0) { memmove(c->out, c->out + n, c->out_n - (size_t)n); c->out_n -= (size_t)n; }
        else if (n < 0 && dw_wouldblock()) break;
        else { c->close_after_flush = 1; c->out_n = 0; return; }
    }
}

static void send_msg(int ci, const DwMsg *m) {
    Conn *c = &conns[ci];
    uint8_t b[DW_MAX_FRAME_LEN + 2];
    int n = dw_encode(m, b, sizeof b);
    if (n < 0 || c->state == S_FREE) return;
    if (c->out_n + (size_t)n > OUTBUF) { c->close_after_flush = 1; c->out_n = 0; return; } /* slow reader: drop */
    memcpy(c->out + c->out_n, b, (size_t)n); c->out_n += (size_t)n;
    flush_conn(ci);
}

static void send_error(int ci, int code) {
    DwMsg m; memset(&m, 0, sizeof m); m.type = DW_S_ERROR; m.u.error.code = (uint8_t)code; send_msg(ci, &m);
    conns[ci].close_after_flush = 1;
}

static void send_round_start(Match *mt) {
    for (int s = 0; s < 2; s++) {
        DwView v; dw_match_view(&mt->m, s, &v);
        DwMsg m; memset(&m, 0, sizeof m); m.type = DW_S_ROUND_START;
        m.u.round_start.round = (uint8_t)v.round; m.u.round_start.hull_you = v.hull_you; m.u.round_start.hull_opp = v.hull_opp;
        m.u.round_start.energy_you = v.energy_you; m.u.round_start.energy_opp = v.energy_opp;
        memcpy(m.u.round_start.hand, v.hand, 4); m.u.round_start.opp_hand_size = v.opp_hand_size;
        m.u.round_start.deadline_ms = opt_ff ? 0 : (uint16_t)opt_round_ms;
        m.u.round_start.armor_you = v.armor_you; m.u.round_start.armor_opp = v.armor_opp;
        m.u.round_start.vault_you = v.vault_you; m.u.round_start.vault_opp = v.vault_opp;
        m.u.round_start.lock_mask = v.lock_mask; m.u.round_start.status_you = v.status_you; m.u.round_start.status_opp = v.status_opp;
        send_msg(mt->conn[s], &m);
    }
    mt->deadline = opt_ff ? 0 : dw_now_ms() + (uint64_t)opt_round_ms;
}

static void write_match_log(const Match *mt) {
    if (!opt_log_dir) return;
    char path[512]; snprintf(path, sizeof path, "%s/matches.ndjson", opt_log_dir);
    FILE *f = fopen(path, "a"); if (!f) return;
    const Conn *a = &conns[mt->conn[0]], *b = &conns[mt->conn[1]];
    fprintf(f, "{\"match_id\":%u,\"seed\":%u,\"names\":[\"%s\",\"%s\"],\"kinds\":[%d,%d],\"plays\":[", mt->id, mt->m.seed, a->name, b->name, a->kind, b->kind);
    for (int i = 0; i < mt->nplays; i++) fprintf(f, "%s[%d,%d]", i ? "," : "", mt->plays[i][0], mt->plays[i][1]);
    fprintf(f, "],\"result\":[%d,%d],\"reason\":%d,\"rounds\":%d", mt->m.result[0], mt->m.result[1], mt->m.reason, mt->m.round);
    if (mt->mode == DW_MODE_DRAFT) {
        fprintf(f, ",\"mode\":\"draft\",\"deck_ids\":[%u,%u],\"decks\":[", mt->deck_id[0], mt->deck_id[1]);
        for (int s = 0; s < 2; s++) { fprintf(f, "%s[", s ? "," : ""); for (int i = 0; i < DW_DRAFT_DECK; i++) fprintf(f, "%s%d", i ? "," : "", mt->deck[s][i]); fprintf(f, "]"); }
        fprintf(f, "]");
    }
    fprintf(f, "}\n");
    fclose(f);
    if (mt->mode == DW_MODE_DRAFT) {   /* per-deck result record so a deck's record can be looked up by deck_id */
        snprintf(path, sizeof path, "%s/decks.ndjson", opt_log_dir);
        FILE *g = fopen(path, "a"); if (!g) return;
        for (int s = 0; s < 2; s++) {
            const Conn *c = s ? b : a;
            fprintf(g, "{\"event\":\"match\",\"deck_id\":%u,\"match_id\":%u,\"player\":\"%s\",\"kind\":%d,\"result\":\"%s\",\"rounds\":%d}\n",
                    mt->deck_id[s], mt->id, c->name, c->kind, mt->m.result[s] == DW_RES_WIN ? "win" : mt->m.result[s] == DW_RES_LOSS ? "loss" : "draw", mt->m.round);
        }
        fclose(g);
    }
}

static void end_match(int mi) {
    Match *mt = &matches[mi];
    for (int s = 0; s < 2; s++) {
        int ci = mt->conn[s];
        DwMsg m; memset(&m, 0, sizeof m); m.type = DW_S_MATCH_END;
        m.u.match_end.match_id = mt->id; m.u.match_end.result = mt->m.result[s]; m.u.match_end.reason = mt->m.reason;
        send_msg(ci, &m);
        if (conns[ci].state == S_IN_MATCH) { conns[ci].state = S_READY; conns[ci].match = -1; }
    }
    int kb = conns[mt->conn[0]].kind + conns[mt->conn[1]].kind;
    st_matches++; if (mt->mode == DW_MODE_DRAFT) st_draft_matches++; if (kb == 2) st_bot_bot++; else if (kb == 1) st_human_bot++; else st_human_human++;
    if (mt->m.reason == DW_END_FORFEIT) st_forfeits++;
    if (opt_iduna && mt->m.reason != DW_END_SERVER && conns[mt->conn[0]].player_id[0] && conns[mt->conn[1]].player_id[0]) {
        Job j; memset(&j, 0, sizeof j); j.type = J_REPORT;
        j.rep.match_id = mt->id; j.rep.seed = mt->m.seed; j.rep.rounds = mt->m.round; j.rep.reason = mt->m.reason; j.rep.mode = mt->mode;
        j.rep.winner = mt->m.result[0] == DW_RES_WIN ? 0 : mt->m.result[1] == DW_RES_WIN ? 1 : 2;
        snprintf(j.rep.seat_pid[0], sizeof j.rep.seat_pid[0], "%s", conns[mt->conn[0]].player_id);
        snprintf(j.rep.seat_pid[1], sizeof j.rep.seat_pid[1], "%s", conns[mt->conn[1]].player_id);
        if (!enqueue_job(&j)) fprintf(stderr, "dw_server: IDUNA job queue full, match %u result not reported (see match log)\n", mt->id);
    }
    write_match_log(mt);
    vlog("match %u ended: result=[%d,%d] reason=%d rounds=%d", mt->id, mt->m.result[0], mt->m.result[1], mt->m.reason, mt->m.round);
    mt->active = 0;
}

static void resolve_round(int mi) {
    Match *mt = &matches[mi]; DwOutcome o;
    if (mt->nplays < DW_MAX_PLAYS) { mt->plays[mt->nplays][0] = mt->m.lock_slot[0]; mt->plays[mt->nplays][1] = mt->m.lock_slot[1]; mt->nplays++; }
    dw_match_resolve(&mt->m, &o);
    for (int s = 0; s < 2; s++) {
        DwMsg m; memset(&m, 0, sizeof m); m.type = DW_S_ROUND_RESULT;
        m.u.round_result.round = (uint8_t)mt->m.round;
        m.u.round_result.card_you = o.card[s]; m.u.round_result.card_opp = o.card[1 - s];
        m.u.round_result.dmg_you = o.dmg_to[s]; m.u.round_result.dmg_opp = o.dmg_to[1 - s];
        m.u.round_result.hull_you = mt->m.hull[s]; m.u.round_result.hull_opp = mt->m.hull[1 - s];
        DwPostView pv; dw_match_post_view(&mt->m, s, &pv);
        m.u.round_result.eff_you = o.eff[s]; m.u.round_result.eff_opp = o.eff[1 - s];
        m.u.round_result.armor_you = pv.armor_you; m.u.round_result.armor_opp = pv.armor_opp;
        m.u.round_result.vault_you = pv.vault_you; m.u.round_result.vault_opp = pv.vault_opp;
        m.u.round_result.heal_you = o.heal[s]; m.u.round_result.heal_opp = o.heal[1 - s];
        m.u.round_result.roll_you = o.roll[s]; m.u.round_result.roll_opp = o.roll[1 - s];
        m.u.round_result.flags_you = o.flags[s]; m.u.round_result.flags_opp = o.flags[1 - s];
        send_msg(mt->conn[s], &m);
    }
    if (mt->m.done) { end_match(mi); return; }
    dw_match_begin_round(&mt->m);
    send_round_start(mt);
}

static int start_match(int a, int b, int mode) {
    int mi = -1;
    for (int i = 0; i < MAX_MATCHES; i++) if (!matches[i].active) { mi = i; break; }
    if (mi < 0) return 0;
    Match *mt = &matches[mi]; memset(mt, 0, sizeof *mt);
    mt->active = 1; mt->id = next_match++; mt->conn[0] = a; mt->conn[1] = b;
    mt->mode = mode;
    if (mode == DW_MODE_DRAFT) {
        for (int s = 0; s < 2; s++) { memcpy(mt->deck[s], conns[mt->conn[s]].deck, DW_DRAFT_DECK); mt->deck_id[s] = conns[mt->conn[s]].deck_id; }
        dw_match_init_decks(&mt->m, next_seed(), mt->deck[0], DW_DRAFT_DECK, mt->deck[1], DW_DRAFT_DECK);
    } else dw_match_init(&mt->m, next_seed());
    for (int s = 0; s < 2; s++) {
        int ci = mt->conn[s], oi = mt->conn[1 - s];
        conns[ci].state = S_IN_MATCH; conns[ci].match = mi; conns[ci].seat = s;
        DwMsg m; memset(&m, 0, sizeof m); m.type = DW_S_MATCH_FOUND;
        m.u.match_found.match_id = mt->id; m.u.match_found.seed = mt->m.seed; m.u.match_found.seat = (uint8_t)s;
        memcpy(m.u.match_found.opp_name, conns[oi].name, DW_NAME_LEN + 1); m.u.match_found.opp_kind = conns[oi].kind;
        send_msg(ci, &m);
    }
    vlog("match %u%s: %s(%d) vs %s(%d) seed=%u", mt->id, mode == DW_MODE_DRAFT ? " [draft]" : "", conns[a].name, conns[a].kind, conns[b].name, conns[b].kind, mt->m.seed);
    dw_match_begin_round(&mt->m);
    send_round_start(mt);
    return 1;
}

/* oldest queued conn of a kind, optionally skipping one; -1 if none. A conn carrying a
 * match_token is excluded from ordinary FIFO/bot pairing -- it's reserved for the specific
 * duel partner presenting the identical token (find_token_pair, checked before this ever runs),
 * not fair game for a stranger just because that partner hasn't queued up yet. Without this
 * exclusion a lone duelist queuing into an already-busy queue would get FIFO-matched with a
 * stranger before their friend even connects, defeating the entire point of Duel Phase 2. */
static int oldest_queued(int mode, int kind, int skip, int *count) {
    int best = -1, n = 0;
    for (int i = 0; i < MAX_CONNS; i++) {
        Conn *c = &conns[i];
        if (c->state != S_QUEUED || c->kind != kind || c->mode != mode || c->match_token[0]) continue;
        n++;
        if (i != skip && (best < 0 || c->queued_seq < conns[best].queued_seq)) best = i;
    }
    if (count) *count = n;
    return best;
}

/* S537 Duel Phase 2: two queued connections in the same mode presenting an identical, non-empty
 * match_token (minted by IDUNA on duel accept -- game_social.go's duelRespond) are a friendly-
 * challenge pair, not strangers in the FIFO line. The token is trusted at face value, the same
 * client-reported trust level as same_deck; the server never calls back to IDUNA to validate it
 * -- both players already went through real IDUNA auth to obtain one, so this is a pairing
 * convenience, not a second authorization check. */
static int find_token_pair(int mode, int *out_a, int *out_b) {
    for (int i = 0; i < MAX_CONNS; i++) {
        Conn *ci = &conns[i];
        if (ci->state != S_QUEUED || ci->mode != mode || !ci->match_token[0]) continue;
        for (int j = i + 1; j < MAX_CONNS; j++) {
            Conn *cj = &conns[j];
            if (cj->state != S_QUEUED || cj->mode != mode || !cj->match_token[0]) continue;
            if (strcmp(ci->match_token, cj->match_token) == 0) { *out_a = i; *out_b = j; return 1; }
        }
    }
    return 0;
}

/* Pairing rule: humans first (human-human, then human-oldest-bot); bots only pair with each other while at
 * least one other bot remains waiting, so a late-joining human always finds a bot. Token-paired duelists
 * (find_token_pair) always go first, ahead of this normal FIFO/bot logic. */
static void try_pair_mode(int mode) {
    for (;;) {
        int ta, tb;
        if (find_token_pair(mode, &ta, &tb)) { if (!start_match(ta, tb, mode)) break; continue; }
        int nh, nb;
        int h1 = oldest_queued(mode, DW_KIND_HUMAN, -1, &nh);
        int b1 = oldest_queued(mode, DW_KIND_BOT, -1, &nb);
        if (nh >= 2) { int h2 = oldest_queued(mode, DW_KIND_HUMAN, h1, NULL); if (!start_match(h1, h2, mode)) break; continue; }
        if (nh == 1 && nb >= 1) { if (!start_match(h1, b1, mode)) break; continue; }
        if (nh == 0 && nb >= 3) { int b2 = oldest_queued(mode, DW_KIND_BOT, b1, NULL); if (!start_match(b1, b2, mode)) break; continue; }
        break;
    }
}
/* Random and draft are separate queues: a player only ever meets someone queued in the same mode. */
static void try_pair(void) { try_pair_mode(DW_MODE_CARD); try_pair_mode(DW_MODE_DRAFT); }

static int queued_count(int mode) { int n = 0; for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state == S_QUEUED && conns[i].mode == mode) n++; return n; }

static void leave_match_or_queue(int ci) {
    Conn *c = &conns[ci];
    if (c->state == S_IN_MATCH && c->match >= 0 && matches[c->match].active) {
        Match *mt = &matches[c->match];
        dw_match_forfeit(&mt->m, c->seat);
        end_match(c->match);
    }
    if (c->state == S_QUEUED || c->state == S_DRAFTING) c->state = S_READY;
}

static void conn_close(int ci) {
    Conn *c = &conns[ci];
    if (c->state == S_FREE) return;
    leave_match_or_queue(ci);
    dw_close(c->fd);
    memset(c, 0, sizeof *c); c->fd = DW_BAD_SOCK; c->state = S_FREE; c->match = -1;
}

static void become_ready(int ci) {
    Conn *c = &conns[ci];
    DwMsg r; memset(&r, 0, sizeof r);
    c->state = S_READY;
    r.type = DW_S_WELCOME; r.u.welcome.session_id = c->session_id;
    r.u.welcome.flags = (uint8_t)((opt_ff ? DW_FLAG_FAST_FORWARD : 0) | (opt_noauth ? 0 : DW_FLAG_AUTH_REQUIRED));
    send_msg(ci, &r);
}

/* Tokens go into an HTTP header and bot names into a JSON body: allow only the characters real ones contain, so a
 * hostile client can't inject headers/JSON. */
static int token_ok(const uint8_t *t, unsigned n) {
    if (n == 0) return 0;
    for (unsigned i = 0; i < n; i++) if (!((t[i] >= 'A' && t[i] <= 'Z') || (t[i] >= 'a' && t[i] <= 'z') || (t[i] >= '0' && t[i] <= '9') || t[i] == '.' || t[i] == '-' || t[i] == '_')) return 0;
    return 1;
}

static void start_verify(int ci, const uint8_t *tok, unsigned n) {
    Conn *c = &conns[ci];
    Job j; memset(&j, 0, sizeof j); j.type = J_VERIFY; j.conn = ci; j.gen = c->session_id;
    if (!token_ok(tok, n)) { send_error(ci, DW_ERR_AUTH); return; }
    memcpy(j.token, tok, n);
    if (c->kind == DW_KIND_BOT) {
        for (const char *q = c->name; *q; q++) if (!((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9') || *q == '_' || *q == '-')) { send_error(ci, DW_ERR_AUTH); return; }
        snprintf(j.name, sizeof j.name, "%s", c->name);
    }
    c->state = S_VERIFYING;
    if (!enqueue_job(&j)) { vlog("IDUNA queue unavailable, refusing conn %d", ci); send_error(ci, DW_ERR_AUTH); }
}

static __attribute__((unused)) void verify_done(const VResult *r) {
    if (r->conn < 0 || r->conn >= MAX_CONNS) return;
    Conn *c = &conns[r->conn];
    if (c->state != S_VERIFYING || c->session_id != r->gen) return;   /* connection went away meanwhile */
    if (r->rc == 0) {
        if ((int)c->kind != r->id.kind) { vlog("conn %d: token kind mismatch", r->conn); send_error(r->conn, DW_ERR_AUTH); return; }
        snprintf(c->player_id, sizeof c->player_id, "%s", r->id.player_id);
        if (c->kind == DW_KIND_HUMAN && r->id.display_name[0]) snprintf(c->name, sizeof c->name, "%.16s", r->id.display_name);
        become_ready(r->conn);
    } else if (r->rc == -1 && opt_fail_open) {
        fprintf(stderr, "dw_server: IDUNA unreachable; --auth-fail-open admits %s unverified (no stats)\n", c->name);
        become_ready(r->conn);
    } else {
        vlog("conn %d: IDUNA %s", r->conn, r->rc == -1 ? "unreachable (failing closed)" : "rejected the token");
        send_error(r->conn, DW_ERR_AUTH);
    }
}

static void drain_results(void) {
#ifdef DW_HAVE_WORKER
    char b[64]; while (read(wake_fd[0], b, sizeof b) > 0) {}
    for (;;) {
        VResult r; int have = 0;
        pthread_mutex_lock(&jm);
        if (rq_n > 0) { r = rq[0]; memmove(rq, rq + 1, (size_t)(rq_n - 1) * sizeof rq[0]); rq_n--; have = 1; }
        pthread_mutex_unlock(&jm);
        if (!have) break;
        verify_done(&r);
    }
#endif
}

/* Deck ids must stay unique across server restarts (decks.ndjson is append-only and WOTAN keys on deck_id): continue after the
 * highest id already logged. */
static void resume_deck_ids(void) {
    if (!opt_log_dir) return;
    char path[512]; snprintf(path, sizeof path, "%s/decks.ndjson", opt_log_dir);
    FILE *f = fopen(path, "r"); if (!f) return;
    static char line[4096]; unsigned long best = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, "\"event\":\"draft\"")) continue;
        char *p = strstr(line, "\"deck_id\":"); if (!p) continue;
        unsigned long v = strtoul(p + 10, NULL, 10); if (v > best) best = v;
    }
    fclose(f);
    if (best >= next_deck) next_deck = (uint32_t)best + 1;
}

static void send_offer(int ci) {
    Conn *c = &conns[ci]; DwMsg r; memset(&r, 0, sizeof r);
    r.type = DW_S_DRAFT_OFFER; r.u.draft_offer.pick_no = c->draft.pick_no; r.u.draft_offer.total = DW_DRAFT_PICKS;
    r.u.draft_offer.card[0] = c->draft.offer[0]; r.u.draft_offer.card[1] = c->draft.offer[1];
    for (int i = 0; i < 3; i++) r.u.draft_offer.left[i] = c->draft.left[i];
    send_msg(ci, &r);
}

/* decks.ndjson: one "draft" record per completed deck (card ids + names), then one "match" record per game played with it
 * (see write_match_log), so any deck can be looked up by deck_id and its record summed up. */
static void log_deck(const Conn *c) {
    if (!opt_log_dir) return;
    char path[512]; snprintf(path, sizeof path, "%s/decks.ndjson", opt_log_dir);
    FILE *f = fopen(path, "a"); if (!f) return;
    fprintf(f, "{\"event\":\"draft\",\"deck_id\":%u,\"t\":%lld,\"player\":\"%s\",\"kind\":%d,\"cards\":[", c->deck_id, (long long)time(NULL), c->name, c->kind);
    for (int i = 0; i < c->deck_n; i++) fprintf(f, "%s%d", i ? "," : "", c->deck[i]);
    fprintf(f, "],\"names\":[");
    for (int i = 0; i < c->deck_n; i++) fprintf(f, "%s\"%s\"", i ? "," : "", dw_card_name(c->deck[i]));
    fprintf(f, "]}\n");
    fclose(f);
}

static void start_draft(int ci) {
    Conn *c = &conns[ci];
    dw_draft_init(&c->draft, next_seed());
    c->state = S_DRAFTING;
    send_offer(ci);
}

static void enter_queue(int ci) {
    Conn *c = &conns[ci]; DwMsg r; memset(&r, 0, sizeof r);
    c->state = S_QUEUED; c->queued_seq = ++queue_seq;
    r.type = DW_S_QUEUED; r.u.queued.waiting = (uint16_t)queued_count(c->mode); send_msg(ci, &r);
    try_pair();
}

static void draft_pick(int ci, const DwMsg *m) {
    Conn *c = &conns[ci];
    if (c->state != S_DRAFTING) { send_error(ci, DW_ERR_BAD_STATE); return; }
    if (dw_draft_pick(&c->draft, m->u.draft_pick.index, m->u.draft_pick.mult) != 0) { send_offer(ci); return; }   /* invalid pick: just re-show the offer */
    if (!dw_draft_done(&c->draft)) { send_offer(ci); return; }
    memcpy(c->deck, c->draft.deck, DW_DRAFT_DECK); c->deck_n = DW_DRAFT_DECK; c->deck_id = next_deck++;
    st_drafts++;
    log_deck(c);
    DwMsg r; memset(&r, 0, sizeof r);
    r.type = DW_S_DRAFT_DONE; r.u.draft_done.deck_id = c->deck_id; memcpy(r.u.draft_done.cards, c->deck, DW_DRAFT_DECK);
    send_msg(ci, &r);
    enter_queue(ci);
}

static void handle_msg(int ci, const DwMsg *m) {
    Conn *c = &conns[ci];
    DwMsg r; memset(&r, 0, sizeof r);
    switch (m->type) {
    case DW_C_PING: r.type = DW_S_PONG; r.u.ping.nonce = m->u.ping.nonce; send_msg(ci, &r); return;
    case DW_C_HELLO:
        if (c->state != S_CONNECTED) { send_error(ci, DW_ERR_BAD_STATE); return; }
        if (m->u.hello.proto != DW_PROTO_VERSION) { send_error(ci, DW_ERR_BAD_PROTO); return; }
        if (m->u.hello.mode != DW_MODE_CARD && m->u.hello.mode != DW_MODE_DRAFT) { send_error(ci, DW_ERR_BAD_STATE); return; } /* backpack = VS1 */
        if (m->u.hello.kind > DW_KIND_BOT) { send_error(ci, DW_ERR_BAD_FRAME); return; }
        memcpy(c->name, m->u.hello.name, sizeof c->name); c->kind = m->u.hello.kind; c->mode = m->u.hello.mode;
        for (char *q = c->name; *q; q++) if ((unsigned char)*q < 32 || *q == '"' || *q == '\\' || *q == 127) *q = '_';
        c->session_id = next_session++;
        if (opt_noauth) { become_ready(ci); return; }
        if (m->u.hello.token_len > 0) { start_verify(ci, m->u.hello.token, m->u.hello.token_len); return; }
        c->state = S_NEEDAUTH;
        return;
    case DW_C_AUTH:
        if (c->state == S_NEEDAUTH) { start_verify(ci, m->u.auth.token, m->u.auth.token_len); return; }
        if (c->state == S_READY && opt_noauth) return;   /* clients may send AUTH unconditionally */
        send_error(ci, DW_ERR_BAD_STATE);
        return;
    case DW_C_QUEUE:
        if (c->state != S_READY) { send_error(ci, DW_ERR_BAD_STATE); return; }
        /* Stash the match_token (if any) on the Conn before the draft/direct-queue fork -- a
         * fresh draft's DRAFT_PICK messages don't carry it, so it has to survive on the
         * connection itself until draft_pick()'s own enter_queue() call at the end. */
        if (m->u.queue.has_match_token) memcpy(c->match_token, m->u.queue.match_token, sizeof c->match_token);
        else c->match_token[0] = 0;
        if (c->mode == DW_MODE_DRAFT && !(m->u.queue.same_deck && c->deck_n == DW_DRAFT_DECK)) { start_draft(ci); return; }   /* redraft (or no deck yet) */
        enter_queue(ci);
        return;
    case DW_C_DRAFT_PICK:
        draft_pick(ci, m);
        return;
    case DW_C_DRAFT_RESUME:
        /* S510 "Resume Uplink": a persisted (IDUNA-side) deck arriving instead of a fresh draft --
         * real legality check (dw_draft_deck_valid), never trust the client's own bucket math. */
        if (c->state != S_READY || c->mode != DW_MODE_DRAFT) { send_error(ci, DW_ERR_BAD_STATE); return; }
        if (!dw_draft_deck_valid(m->u.draft_resume.cards, DW_DRAFT_DECK)) { send_error(ci, DW_ERR_BAD_FRAME); return; }
        memcpy(c->deck, m->u.draft_resume.cards, DW_DRAFT_DECK); c->deck_n = DW_DRAFT_DECK; c->deck_id = next_deck++;
        log_deck(c);
        r.type = DW_S_DRAFT_DONE; r.u.draft_done.deck_id = c->deck_id; memcpy(r.u.draft_done.cards, c->deck, DW_DRAFT_DECK);
        send_msg(ci, &r);
        return;
    case DW_C_PLAY: {
        if (c->state != S_IN_MATCH || c->match < 0 || !matches[c->match].active) {
            r.type = DW_S_PLAY_REJECT; r.u.reject.match_id = m->u.play.match_id; r.u.reject.round = m->u.play.round; r.u.reject.reason = DW_REJ_NO_MATCH;
            send_msg(ci, &r); return;
        }
        Match *mt = &matches[c->match];
        r.u.reject.match_id = m->u.play.match_id; r.u.reject.round = m->u.play.round;
        if (m->u.play.match_id != mt->id) { r.type = DW_S_PLAY_REJECT; r.u.reject.reason = DW_REJ_NO_MATCH; send_msg(ci, &r); return; }
        if (m->u.play.round != mt->m.round) { r.type = DW_S_PLAY_REJECT; r.u.reject.reason = DW_REJ_WRONG_ROUND; send_msg(ci, &r); return; }
        int rc = dw_match_lock(&mt->m, c->seat, m->u.play.slot);
        if (rc) { r.type = DW_S_PLAY_REJECT; r.u.reject.reason = (uint8_t)rc; send_msg(ci, &r); return; }
        memset(&r, 0, sizeof r); r.type = DW_S_PLAY_ACK; r.u.ack.match_id = mt->id; r.u.ack.round = (uint8_t)mt->m.round; send_msg(ci, &r);
        if (dw_match_both_locked(&mt->m)) resolve_round((int)(mt - matches));
        return;
    }
    case DW_C_LEAVE:
        if (c->state == S_CONNECTED || c->state == S_NEEDAUTH || c->state == S_VERIFYING) { send_error(ci, DW_ERR_BAD_STATE); return; }
        leave_match_or_queue(ci);
        return;
    default: send_error(ci, DW_ERR_BAD_STATE); return;
    }
}

static void read_conn(int ci) {
    Conn *c = &conns[ci];
    long n = dw_recv(c->fd, c->in + c->in_n, INBUF - c->in_n);
    if (n == 0) { conn_close(ci); return; }
    if (n < 0) { if (!dw_wouldblock()) conn_close(ci); return; }
    c->in_n += (size_t)n;
    size_t off = 0;
    while (off < c->in_n && !c->close_after_flush) {
        DwMsg m; size_t used = 0;
        int r = dw_decode(c->in + off, c->in_n - off, &m, &used);
        if (r == 0) break;
        if (r < 0) { send_error(ci, DW_ERR_BAD_FRAME); break; }
        off += used;
        handle_msg(ci, &m);
        if (conns[ci].state == S_FREE) return;
    }
    if (off) { memmove(c->in, c->in + off, c->in_n - off); c->in_n -= off; }
    if (c->in_n >= INBUF && !c->close_after_flush) send_error(ci, DW_ERR_BAD_FRAME);
}

static void accept_conns(dw_sock ls) {
    for (;;) {
        dw_sock fd = accept(ls, NULL, NULL);
        if (fd == DW_BAD_SOCK) return;
        int slot = -1;
        for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state == S_FREE) { slot = i; break; }
        if (slot < 0) { dw_close(fd); continue; }
        dw_nonblock(fd);
        int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
        Conn *c = &conns[slot]; memset(c, 0, sizeof *c);
        c->fd = fd; c->state = S_CONNECTED; c->match = -1; c->connect_ms = dw_now_ms();
    }
}

static void expire_timers(void) {
    uint64_t now = dw_now_ms();
    for (int i = 0; i < MAX_CONNS; i++)
        if ((conns[i].state == S_CONNECTED || conns[i].state == S_NEEDAUTH || conns[i].state == S_VERIFYING) && now - conns[i].connect_ms > HELLO_TIMEOUT_MS) conn_close(i);
    if (opt_ff) return;
    for (int i = 0; i < MAX_MATCHES; i++) {
        Match *mt = &matches[i];
        if (!mt->active || mt->deadline == 0 || now < mt->deadline) continue;
        for (int s = 0; s < 2; s++) if (!mt->m.locked[s]) dw_match_lock(&mt->m, s, -1); /* timeout = pass */
        resolve_round(i);
    }
}

int main(int argc, char **argv) {
    int port = 7700; const char *bind_addr = "0.0.0.0", *iduna_url = NULL, *secret_file = NULL, *agent_name = "DEADWEIGHT-SERVER";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("dw_server %s (rules: %d cards, hull %d)\n", DW_VERSION, num_cards(), start_hull()); return 0; }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bind") && i + 1 < argc) bind_addr = argv[++i];
        else if (!strcmp(argv[i], "--fast-forward")) opt_ff = 1;
        else if (!strcmp(argv[i], "--no-auth")) { opt_noauth = 1; opt_noauth_explicit = 1; }
        else if (!strcmp(argv[i], "--iduna-url") && i + 1 < argc) iduna_url = argv[++i];
        else if (!strcmp(argv[i], "--agent-secret-file") && i + 1 < argc) secret_file = argv[++i];
        else if (!strcmp(argv[i], "--agent-name") && i + 1 < argc) agent_name = argv[++i];
        else if (!strcmp(argv[i], "--auth-fail-open")) opt_fail_open = 1;
        else if (!strcmp(argv[i], "--match-log") && i + 1 < argc) opt_log_dir = argv[++i];
        else if (!strcmp(argv[i], "--max-matches") && i + 1 < argc) opt_max_matches = atol(argv[++i]);
        else if (!strcmp(argv[i], "--round-ms") && i + 1 < argc) { opt_round_ms = atoi(argv[++i]); if (opt_round_ms < 50 || opt_round_ms > 60000) { fprintf(stderr, "--round-ms must be 50..60000\n"); return 2; } }
        else if (!strcmp(argv[i], "--verbose")) opt_verbose = 1;
        else {
            fprintf(stderr, "usage: dw_server [--port N] [--bind ADDR] [--fast-forward] [--no-auth] [--iduna-url URL --agent-secret-file F [--agent-name N] [--auth-fail-open]] [--match-log DIR] [--max-matches N] [--round-ms N] [--verbose] [--version]\n");
            return 2;
        }
    }
    if (iduna_url) {
#ifndef DW_HAVE_WORKER
        fprintf(stderr, "dw_server: --iduna-url is not supported in this (Windows) build; use --no-auth\n"); return 2;
#endif
        int rc = dwi_configure(&iduna, iduna_url, agent_name, secret_file);
        if (rc == -1) { fprintf(stderr, "dw_server: bad --iduna-url %s\n", iduna_url); return 2; }
        if (rc != 0) { fprintf(stderr, "dw_server: cannot read agent secret for %s from %s (rc %d)\n", agent_name, secret_file ? secret_file : "(no --agent-secret-file)", rc); return 1; }
        opt_iduna = 1;
        if (!opt_noauth_explicit) opt_noauth = 0;
    }
    if (dw_net_init() != 0) { fprintf(stderr, "dw_server: net init failed\n"); return 1; }
    if (opt_iduna && start_worker() != 0) { fprintf(stderr, "dw_server: cannot start IDUNA worker\n"); return 1; }
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    for (int i = 0; i < MAX_CONNS; i++) { conns[i].fd = DW_BAD_SOCK; conns[i].match = -1; }
    dw_sock ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == DW_BAD_SOCK) { fprintf(stderr, "dw_server: socket failed\n"); return 1; }
    int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, bind_addr, &sa.sin_addr) != 1) { fprintf(stderr, "dw_server: bad --bind %s\n", bind_addr); return 2; }
    if (bind(ls, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(ls, 64) != 0) { fprintf(stderr, "dw_server: bind/listen on %s:%d failed\n", bind_addr, port); return 1; }
    if (port == 0) { socklen_t sl = sizeof sa; getsockname(ls, (struct sockaddr *)&sa, &sl); port = ntohs(sa.sin_port); }
    dw_nonblock(ls);
    resume_deck_ids();
    seed_state ^= (uint32_t)dw_now_ms() | 1u;
    printf("dw_server %s listening on %s:%d%s%s\n", DW_VERSION, bind_addr, port, opt_ff ? " (fast-forward)" : "", opt_noauth ? " (no-auth)" : " (IDUNA auth required)"); fflush(stdout);

    while (!stop_flag && !(opt_max_matches >= 0 && st_matches >= opt_max_matches)) {
        struct pollfd pf[MAX_CONNS + 2]; int map[MAX_CONNS + 2]; unsigned n = 0;
        pf[n].fd = ls; pf[n].events = POLLIN; pf[n].revents = 0; map[n++] = -1;
#ifdef DW_HAVE_WORKER
        if (opt_iduna) { pf[n].fd = wake_fd[0]; pf[n].events = POLLIN; pf[n].revents = 0; map[n++] = -2; }
#endif
        for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state != S_FREE) {
            pf[n].fd = conns[i].fd; pf[n].events = (short)(POLLIN | (conns[i].out_n ? POLLOUT : 0)); pf[n].revents = 0; map[n++] = i;
        }
        int rc = dw_poll(pf, n, 200);
        if (rc > 0) {
            for (unsigned k = 0; k < n; k++) {
                if (!pf[k].revents) continue;
                if (map[k] == -1) { accept_conns(ls); continue; }
                if (map[k] == -2) { drain_results(); continue; }
                int ci = map[k];
                if (conns[ci].state == S_FREE) continue;
                if (pf[k].revents & POLLOUT) flush_conn(ci);
                if (pf[k].revents & (POLLIN | POLLHUP | POLLERR)) read_conn(ci);
            }
        }
        for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state != S_FREE && conns[i].close_after_flush) {
            flush_conn(i); conn_close(i);
        }
        expire_timers();
        if (opt_iduna) drain_results();
    }
    for (int i = 0; i < MAX_MATCHES; i++) if (matches[i].active) {
        dw_match_forfeit(&matches[i].m, 0); matches[i].m.result[0] = matches[i].m.result[1] = DW_RES_DRAW; matches[i].m.reason = DW_END_SERVER;
        end_match(i);
    }
    for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state != S_FREE) { flush_conn(i); conn_close(i); }
    dw_close(ls);
    stop_worker();
    fprintf(stderr, "dw_server: shutdown drafts=%ld draft_matches=%ld matches=%ld bot_bot=%ld human_bot=%ld human_human=%ld forfeits=%ld\n",
            st_drafts, st_draft_matches, st_matches, st_bot_bot, st_human_bot, st_human_human, st_forfeits);
    return 0;
}
