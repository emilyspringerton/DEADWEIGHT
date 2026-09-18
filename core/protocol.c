#include "protocol.h"
#include <string.h>

typedef struct { uint8_t *p; size_t cap, n; int bad; } W;
static void w8(W *w, unsigned v) { if (w->n + 1 > w->cap) { w->bad = 1; return; } w->p[w->n++] = (uint8_t)v; }
static void w16(W *w, unsigned v) { w8(w, v & 255); w8(w, (v >> 8) & 255); }
static void w32(W *w, uint32_t v) { w16(w, v & 0xFFFF); w16(w, v >> 16); }
static void wname(W *w, const char *s) {
    size_t l = 0; while (l < DW_NAME_LEN && s[l]) l++;
    for (size_t i = 0; i < DW_NAME_LEN; i++) w8(w, i < l ? (uint8_t)s[i] : 0);
}

typedef struct { const uint8_t *p; size_t len, n; } R;
static unsigned r8(R *r) { return r->p[r->n++]; }
static unsigned r16(R *r) { unsigned a = r8(r); return a | (r8(r) << 8); }
static uint32_t r32(R *r) { uint32_t a = r16(r); return a | ((uint32_t)r16(r) << 16); }
static void rname(R *r, char *out) {
    memcpy(out, r->p + r->n, DW_NAME_LEN); r->n += DW_NAME_LEN; out[DW_NAME_LEN] = 0;
    /* zero everything after the first NUL so decoded names are canonical */
    size_t l = strlen(out); memset(out + l, 0, DW_NAME_LEN + 1 - l);
}

int dw_encode(const DwMsg *m, uint8_t *buf, size_t cap) {
    W w = { buf, cap, 0, 0 };
    w16(&w, 0); /* len patched below */
    w8(&w, m->type);
    switch (m->type) {
    case DW_C_HELLO:
        if (m->u.hello.token_len > DW_MAX_TOKEN) return -1;
        w8(&w, m->u.hello.proto); w8(&w, m->u.hello.mode); w8(&w, m->u.hello.kind);
        wname(&w, m->u.hello.name); w8(&w, m->u.hello.token_len);
        for (unsigned i = 0; i < m->u.hello.token_len; i++) w8(&w, m->u.hello.token[i]);
        break;
    case DW_C_QUEUE: case DW_C_LEAVE: break;
    case DW_C_AUTH:
        if (m->u.auth.token_len > DW_MAX_AUTH_TOKEN) return -1;
        w16(&w, m->u.auth.token_len);
        for (unsigned i = 0; i < m->u.auth.token_len; i++) w8(&w, m->u.auth.token[i]);
        break;
    case DW_C_PLAY: w32(&w, m->u.play.match_id); w8(&w, m->u.play.round); w8(&w, (uint8_t)m->u.play.slot); break;
    case DW_C_PING: case DW_S_PONG: w32(&w, m->u.ping.nonce); break;
    case DW_S_WELCOME: w32(&w, m->u.welcome.session_id); w8(&w, m->u.welcome.flags); break;
    case DW_S_QUEUED: w16(&w, m->u.queued.waiting); break;
    case DW_S_MATCH_FOUND:
        w32(&w, m->u.match_found.match_id); w32(&w, m->u.match_found.seed); w8(&w, m->u.match_found.seat);
        wname(&w, m->u.match_found.opp_name); w8(&w, m->u.match_found.opp_kind); break;
    case DW_S_ROUND_START:
        w8(&w, m->u.round_start.round); w8(&w, (uint8_t)m->u.round_start.hull_you); w8(&w, (uint8_t)m->u.round_start.hull_opp);
        w8(&w, m->u.round_start.energy_you); w8(&w, m->u.round_start.energy_opp);
        for (int i = 0; i < 4; i++) w8(&w, (uint8_t)m->u.round_start.hand[i]);
        w8(&w, m->u.round_start.opp_hand_size); w16(&w, m->u.round_start.deadline_ms); break;
    case DW_S_PLAY_ACK: w32(&w, m->u.ack.match_id); w8(&w, m->u.ack.round); break;
    case DW_S_PLAY_REJECT: w32(&w, m->u.reject.match_id); w8(&w, m->u.reject.round); w8(&w, m->u.reject.reason); break;
    case DW_S_ROUND_RESULT:
        w8(&w, m->u.round_result.round); w8(&w, (uint8_t)m->u.round_result.card_you); w8(&w, (uint8_t)m->u.round_result.card_opp);
        w8(&w, m->u.round_result.dmg_you); w8(&w, m->u.round_result.dmg_opp);
        w8(&w, (uint8_t)m->u.round_result.hull_you); w8(&w, (uint8_t)m->u.round_result.hull_opp); break;
    case DW_S_MATCH_END: w32(&w, m->u.match_end.match_id); w8(&w, m->u.match_end.result); w8(&w, m->u.match_end.reason); break;
    case DW_S_ERROR: w8(&w, m->u.error.code); break;
    default: return -1;
    }
    if (w.bad || w.n - 2 > DW_MAX_FRAME_LEN) return -1;
    buf[0] = (uint8_t)((w.n - 2) & 255); buf[1] = (uint8_t)((w.n - 2) >> 8);
    return (int)w.n;
}

/* Fixed payload sizes (bytes after the type byte); HELLO is variable (>= 20). */
static int payload_size(uint8_t t) {
    switch (t) {
    case DW_C_HELLO: return -2; case DW_C_AUTH: return -3; case DW_C_QUEUE: case DW_C_LEAVE: return 0; case DW_C_PLAY: return 6;
    case DW_C_PING: case DW_S_PONG: return 4; case DW_S_WELCOME: return 5; case DW_S_QUEUED: return 2;
    case DW_S_MATCH_FOUND: return 26; case DW_S_ROUND_START: return 12; case DW_S_PLAY_ACK: return 5;
    case DW_S_PLAY_REJECT: return 6; case DW_S_ROUND_RESULT: return 7; case DW_S_MATCH_END: return 6;
    case DW_S_ERROR: return 1; default: return -1;
    }
}

int dw_decode(const uint8_t *buf, size_t len, DwMsg *out, size_t *consumed) {
    if (len < 2) return 0;
    size_t flen = (size_t)buf[0] | ((size_t)buf[1] << 8);
    if (flen < 1 || flen > DW_MAX_FRAME_LEN) return -1;
    if (len < 2 + flen) return 0;
    uint8_t type = buf[2];
    int ps = payload_size(type);
    size_t plen = flen - 1;
    if (ps == -1) return -1;
    if (ps >= 0 && (size_t)ps != plen) return -1;
    if (ps == -2 && plen < 20) return -1;
    if (ps == -3 && plen < 2) return -1;
    R r = { buf + 3, plen, 0 };
    memset(out, 0, sizeof *out);
    out->type = type;
    switch (type) {
    case DW_C_HELLO:
        out->u.hello.proto = (uint8_t)r8(&r); out->u.hello.mode = (uint8_t)r8(&r); out->u.hello.kind = (uint8_t)r8(&r);
        rname(&r, out->u.hello.name); out->u.hello.token_len = (uint8_t)r8(&r);
        if (out->u.hello.token_len > DW_MAX_TOKEN || (size_t)out->u.hello.token_len != plen - 20) return -1;
        memcpy(out->u.hello.token, r.p + r.n, out->u.hello.token_len);
        break;
    case DW_C_QUEUE: case DW_C_LEAVE: break;
    case DW_C_AUTH:
        out->u.auth.token_len = (uint16_t)r16(&r);
        if (out->u.auth.token_len > DW_MAX_AUTH_TOKEN || (size_t)out->u.auth.token_len != plen - 2) return -1;
        memcpy(out->u.auth.token, r.p + r.n, out->u.auth.token_len);
        break;
    case DW_C_PLAY: out->u.play.match_id = r32(&r); out->u.play.round = (uint8_t)r8(&r); out->u.play.slot = (int8_t)r8(&r); break;
    case DW_C_PING: case DW_S_PONG: out->u.ping.nonce = r32(&r); break;
    case DW_S_WELCOME: out->u.welcome.session_id = r32(&r); out->u.welcome.flags = (uint8_t)r8(&r); break;
    case DW_S_QUEUED: out->u.queued.waiting = (uint16_t)r16(&r); break;
    case DW_S_MATCH_FOUND:
        out->u.match_found.match_id = r32(&r); out->u.match_found.seed = r32(&r); out->u.match_found.seat = (uint8_t)r8(&r);
        rname(&r, out->u.match_found.opp_name); out->u.match_found.opp_kind = (uint8_t)r8(&r); break;
    case DW_S_ROUND_START:
        out->u.round_start.round = (uint8_t)r8(&r); out->u.round_start.hull_you = (int8_t)r8(&r); out->u.round_start.hull_opp = (int8_t)r8(&r);
        out->u.round_start.energy_you = (uint8_t)r8(&r); out->u.round_start.energy_opp = (uint8_t)r8(&r);
        for (int i = 0; i < 4; i++) out->u.round_start.hand[i] = (int8_t)r8(&r);
        out->u.round_start.opp_hand_size = (uint8_t)r8(&r); out->u.round_start.deadline_ms = (uint16_t)r16(&r); break;
    case DW_S_PLAY_ACK: out->u.ack.match_id = r32(&r); out->u.ack.round = (uint8_t)r8(&r); break;
    case DW_S_PLAY_REJECT: out->u.reject.match_id = r32(&r); out->u.reject.round = (uint8_t)r8(&r); out->u.reject.reason = (uint8_t)r8(&r); break;
    case DW_S_ROUND_RESULT:
        out->u.round_result.round = (uint8_t)r8(&r); out->u.round_result.card_you = (int8_t)r8(&r); out->u.round_result.card_opp = (int8_t)r8(&r);
        out->u.round_result.dmg_you = (uint8_t)r8(&r); out->u.round_result.dmg_opp = (uint8_t)r8(&r);
        out->u.round_result.hull_you = (int8_t)r8(&r); out->u.round_result.hull_opp = (int8_t)r8(&r); break;
    case DW_S_MATCH_END: out->u.match_end.match_id = r32(&r); out->u.match_end.result = (uint8_t)r8(&r); out->u.match_end.reason = (uint8_t)r8(&r); break;
    case DW_S_ERROR: out->u.error.code = (uint8_t)r8(&r); break;
    default: return -1;
    }
    *consumed = 2 + flen;
    return 1;
}
