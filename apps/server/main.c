/* dw_server -- DEADWEIGHT authoritative match server (S503-04). Single poll() loop hosts the queue and many
 * concurrent card-mode matches over TCP (docs/WIRE_PROTOCOL.md). No blocking calls in the loop. */
#include "net.h"
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include "card_rules.h"
#include "match.h"
#include "protocol.h"
#include "version.h"

#define MAX_CONNS 512
#define MAX_MATCHES 256
#define INBUF 1024
#define OUTBUF 8192
#define ROUND_MS_DEFAULT 20000
#define HELLO_TIMEOUT_MS 10000

enum { S_FREE = 0, S_CONNECTED, S_READY, S_QUEUED, S_IN_MATCH };

typedef struct {
    dw_sock fd; int state; uint32_t session_id;
    uint8_t in[INBUF]; size_t in_n;
    uint8_t out[OUTBUF]; size_t out_n;
    char name[DW_NAME_LEN + 1]; uint8_t kind, mode;
    int match, seat; uint64_t queued_seq, connect_ms; int close_after_flush;
} Conn;

typedef struct {
    int active; uint32_t id; DwMatch m; int conn[2];
    uint64_t deadline; int8_t plays[8][2]; int nplays;
} Match;

static Conn conns[MAX_CONNS];
static Match matches[MAX_MATCHES];
static volatile sig_atomic_t stop_flag = 0;
static int opt_ff = 0, opt_noauth = 1, opt_verbose = 0;
static long opt_max_matches = -1;
static int opt_round_ms = ROUND_MS_DEFAULT;
static const char *opt_log_dir = NULL;
static uint32_t next_session = 1, next_match = 1, seed_state = 0x9E3779B9u;
static uint64_t queue_seq = 0;
static long st_matches = 0, st_bot_bot = 0, st_human_bot = 0, st_human_human = 0, st_forfeits = 0;

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
    fprintf(f, "],\"result\":[%d,%d],\"reason\":%d,\"rounds\":%d}\n", mt->m.result[0], mt->m.result[1], mt->m.reason, mt->m.round);
    fclose(f);
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
    st_matches++; if (kb == 2) st_bot_bot++; else if (kb == 1) st_human_bot++; else st_human_human++;
    if (mt->m.reason == DW_END_FORFEIT) st_forfeits++;
    write_match_log(mt);
    vlog("match %u ended: result=[%d,%d] reason=%d rounds=%d", mt->id, mt->m.result[0], mt->m.result[1], mt->m.reason, mt->m.round);
    mt->active = 0;
}

static void resolve_round(int mi) {
    Match *mt = &matches[mi]; DwOutcome o;
    mt->plays[mt->nplays][0] = mt->m.lock_slot[0]; mt->plays[mt->nplays][1] = mt->m.lock_slot[1]; mt->nplays++;
    dw_match_resolve(&mt->m, &o);
    for (int s = 0; s < 2; s++) {
        DwMsg m; memset(&m, 0, sizeof m); m.type = DW_S_ROUND_RESULT;
        m.u.round_result.round = (uint8_t)mt->m.round;
        m.u.round_result.card_you = o.card[s]; m.u.round_result.card_opp = o.card[1 - s];
        m.u.round_result.dmg_you = o.dmg_to[s]; m.u.round_result.dmg_opp = o.dmg_to[1 - s];
        m.u.round_result.hull_you = mt->m.hull[s]; m.u.round_result.hull_opp = mt->m.hull[1 - s];
        send_msg(mt->conn[s], &m);
    }
    if (mt->m.done) { end_match(mi); return; }
    dw_match_begin_round(&mt->m);
    send_round_start(mt);
}

static int start_match(int a, int b) {
    int mi = -1;
    for (int i = 0; i < MAX_MATCHES; i++) if (!matches[i].active) { mi = i; break; }
    if (mi < 0) return 0;
    Match *mt = &matches[mi]; memset(mt, 0, sizeof *mt);
    mt->active = 1; mt->id = next_match++; mt->conn[0] = a; mt->conn[1] = b;
    dw_match_init(&mt->m, next_seed());
    for (int s = 0; s < 2; s++) {
        int ci = mt->conn[s], oi = mt->conn[1 - s];
        conns[ci].state = S_IN_MATCH; conns[ci].match = mi; conns[ci].seat = s;
        DwMsg m; memset(&m, 0, sizeof m); m.type = DW_S_MATCH_FOUND;
        m.u.match_found.match_id = mt->id; m.u.match_found.seed = mt->m.seed; m.u.match_found.seat = (uint8_t)s;
        memcpy(m.u.match_found.opp_name, conns[oi].name, DW_NAME_LEN + 1); m.u.match_found.opp_kind = conns[oi].kind;
        send_msg(ci, &m);
    }
    vlog("match %u: %s(%d) vs %s(%d) seed=%u", mt->id, conns[a].name, conns[a].kind, conns[b].name, conns[b].kind, mt->m.seed);
    dw_match_begin_round(&mt->m);
    send_round_start(mt);
    return 1;
}

/* oldest queued conn of a kind, optionally skipping one; -1 if none */
static int oldest_queued(int kind, int skip, int *count) {
    int best = -1, n = 0;
    for (int i = 0; i < MAX_CONNS; i++) {
        Conn *c = &conns[i];
        if (c->state != S_QUEUED || c->kind != kind) continue;
        n++;
        if (i != skip && (best < 0 || c->queued_seq < conns[best].queued_seq)) best = i;
    }
    if (count) *count = n;
    return best;
}

/* Pairing rule: humans first (human-human, then human-oldest-bot); bots only pair with each other while at
 * least one other bot remains waiting, so a late-joining human always finds a bot. */
static void try_pair(void) {
    for (;;) {
        int nh, nb;
        int h1 = oldest_queued(DW_KIND_HUMAN, -1, &nh);
        int b1 = oldest_queued(DW_KIND_BOT, -1, &nb);
        if (nh >= 2) { int h2 = oldest_queued(DW_KIND_HUMAN, h1, NULL); if (!start_match(h1, h2)) break; continue; }
        if (nh == 1 && nb >= 1) { if (!start_match(h1, b1)) break; continue; }
        if (nh == 0 && nb >= 3) { int b2 = oldest_queued(DW_KIND_BOT, b1, NULL); if (!start_match(b1, b2)) break; continue; }
        break;
    }
}

static int queued_count(void) { int n = 0; for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state == S_QUEUED) n++; return n; }

static void leave_match_or_queue(int ci) {
    Conn *c = &conns[ci];
    if (c->state == S_IN_MATCH && c->match >= 0 && matches[c->match].active) {
        Match *mt = &matches[c->match];
        dw_match_forfeit(&mt->m, c->seat);
        end_match(c->match);
    }
    if (c->state == S_QUEUED) c->state = S_READY;
}

static void conn_close(int ci) {
    Conn *c = &conns[ci];
    if (c->state == S_FREE) return;
    leave_match_or_queue(ci);
    dw_close(c->fd);
    memset(c, 0, sizeof *c); c->fd = DW_BAD_SOCK; c->state = S_FREE; c->match = -1;
}

static void handle_msg(int ci, const DwMsg *m) {
    Conn *c = &conns[ci];
    DwMsg r; memset(&r, 0, sizeof r);
    switch (m->type) {
    case DW_C_PING: r.type = DW_S_PONG; r.u.ping.nonce = m->u.ping.nonce; send_msg(ci, &r); return;
    case DW_C_HELLO:
        if (c->state != S_CONNECTED) { send_error(ci, DW_ERR_BAD_STATE); return; }
        if (m->u.hello.proto != DW_PROTO_VERSION) { send_error(ci, DW_ERR_BAD_PROTO); return; }
        if (m->u.hello.mode != DW_MODE_CARD) { send_error(ci, DW_ERR_BAD_STATE); return; } /* backpack = VS1 */
        if (m->u.hello.kind > DW_KIND_BOT) { send_error(ci, DW_ERR_BAD_FRAME); return; }
        if (!opt_noauth) { send_error(ci, DW_ERR_AUTH); return; } /* IDUNA verification: S503-04 item 8 */
        memcpy(c->name, m->u.hello.name, sizeof c->name); c->kind = m->u.hello.kind; c->mode = m->u.hello.mode;
        c->state = S_READY; c->session_id = next_session++;
        r.type = DW_S_WELCOME; r.u.welcome.session_id = c->session_id;
        r.u.welcome.flags = (uint8_t)((opt_ff ? DW_FLAG_FAST_FORWARD : 0) | (opt_noauth ? 0 : DW_FLAG_AUTH_REQUIRED));
        send_msg(ci, &r);
        return;
    case DW_C_QUEUE:
        if (c->state != S_READY) { send_error(ci, DW_ERR_BAD_STATE); return; }
        c->state = S_QUEUED; c->queued_seq = ++queue_seq;
        r.type = DW_S_QUEUED; r.u.queued.waiting = (uint16_t)queued_count(); send_msg(ci, &r);
        try_pair();
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
        if (c->state == S_CONNECTED) { send_error(ci, DW_ERR_BAD_STATE); return; }
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
        if (conns[i].state == S_CONNECTED && now - conns[i].connect_ms > HELLO_TIMEOUT_MS) conn_close(i);
    if (opt_ff) return;
    for (int i = 0; i < MAX_MATCHES; i++) {
        Match *mt = &matches[i];
        if (!mt->active || mt->deadline == 0 || now < mt->deadline) continue;
        for (int s = 0; s < 2; s++) if (!mt->m.locked[s]) dw_match_lock(&mt->m, s, -1); /* timeout = pass */
        resolve_round(i);
    }
}

int main(int argc, char **argv) {
    int port = 7700; const char *bind_addr = "0.0.0.0";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("dw_server %s (rules: %d cards, hull %d)\n", DW_VERSION, num_cards(), start_hull()); return 0; }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bind") && i + 1 < argc) bind_addr = argv[++i];
        else if (!strcmp(argv[i], "--fast-forward")) opt_ff = 1;
        else if (!strcmp(argv[i], "--no-auth")) opt_noauth = 1;
        else if (!strcmp(argv[i], "--match-log") && i + 1 < argc) opt_log_dir = argv[++i];
        else if (!strcmp(argv[i], "--max-matches") && i + 1 < argc) opt_max_matches = atol(argv[++i]);
        else if (!strcmp(argv[i], "--round-ms") && i + 1 < argc) { opt_round_ms = atoi(argv[++i]); if (opt_round_ms < 50 || opt_round_ms > 60000) { fprintf(stderr, "--round-ms must be 50..60000\n"); return 2; } }
        else if (!strcmp(argv[i], "--verbose")) opt_verbose = 1;
        else {
            fprintf(stderr, "usage: dw_server [--port N] [--bind ADDR] [--fast-forward] [--no-auth] [--match-log DIR] [--max-matches N] [--round-ms N] [--verbose] [--version]\n");
            return 2;
        }
    }
    if (dw_net_init() != 0) { fprintf(stderr, "dw_server: net init failed\n"); return 1; }
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
    seed_state ^= (uint32_t)dw_now_ms() | 1u;
    printf("dw_server %s listening on %s:%d%s\n", DW_VERSION, bind_addr, port, opt_ff ? " (fast-forward)" : ""); fflush(stdout);

    while (!stop_flag && !(opt_max_matches >= 0 && st_matches >= opt_max_matches)) {
        struct pollfd pf[MAX_CONNS + 1]; int map[MAX_CONNS + 1]; unsigned n = 0;
        pf[n].fd = ls; pf[n].events = POLLIN; pf[n].revents = 0; map[n++] = -1;
        for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state != S_FREE) {
            pf[n].fd = conns[i].fd; pf[n].events = (short)(POLLIN | (conns[i].out_n ? POLLOUT : 0)); pf[n].revents = 0; map[n++] = i;
        }
        int rc = dw_poll(pf, n, 200);
        if (rc > 0) {
            for (unsigned k = 0; k < n; k++) {
                if (!pf[k].revents) continue;
                if (map[k] < 0) { accept_conns(ls); continue; }
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
    }
    for (int i = 0; i < MAX_MATCHES; i++) if (matches[i].active) {
        dw_match_forfeit(&matches[i].m, 0); matches[i].m.result[0] = matches[i].m.result[1] = DW_RES_DRAW; matches[i].m.reason = DW_END_SERVER;
        end_match(i);
    }
    for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state != S_FREE) { flush_conn(i); conn_close(i); }
    dw_close(ls);
    fprintf(stderr, "dw_server: shutdown matches=%ld bot_bot=%ld human_bot=%ld human_human=%ld forfeits=%ld\n",
            st_matches, st_bot_bot, st_human_bot, st_human_human, st_forfeits);
    return 0;
}
