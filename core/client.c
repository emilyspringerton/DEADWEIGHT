#include "client.h"
#include "card_rules.h"
#include <stdio.h>
#include <string.h>

int dwc_connect(DwClient *c, const char *host, int port) {
    memset(c, 0, sizeof *c);
    c->fd = dw_connect(host, port);
    if (c->fd == DW_BAD_SOCK) return -1;
    int one = 1; setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    return 0;
}

int dwc_send(DwClient *c, const DwMsg *m) {
    uint8_t b[DW_MAX_FRAME_LEN + 2]; int n = dw_encode(m, b, sizeof b);
    if (n < 0) return -1;
    size_t off = 0;
    while (off < (size_t)n) { long r = dw_send(c->fd, b + off, (size_t)n - off); if (r <= 0) return -1; off += (size_t)r; }
    return 0;
}

int dwc_recv(DwClient *c, DwMsg *m, int timeout_ms) {
    for (;;) {
        size_t used = 0;
        int r = dw_decode(c->in, c->n, m, &used);
        if (r < 0) return -1;
        if (r == 1) { memmove(c->in, c->in + used, c->n - used); c->n -= used; return 1; }
        struct pollfd p; p.fd = c->fd; p.events = POLLIN; p.revents = 0;
        int pr = dw_poll(&p, 1, timeout_ms);
        if (pr == 0) return 0;
        if (pr < 0) return -1;
        long n = dw_recv(c->fd, c->in + c->n, sizeof c->in - c->n);
        if (n <= 0) return -1;
        c->n += (size_t)n;
    }
}

void dwc_close(DwClient *c) { if (c->fd != DW_BAD_SOCK) { dw_close(c->fd); c->fd = DW_BAD_SOCK; } }

/* Bot draft policy (simple on purpose -- the play brain is unchanged): take the higher power-for-cost card of the offer (25%
 * of the time the other one, for variety) and give strong cards the scarcer multiplicity buckets. */
static uint32_t bxs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }
static int bot_score(int id) { return card_power(id) * 2 - card_cost(id); }
static void bot_draft_pick(uint32_t *rng, const DwMsg *offer, DwMsg *out) {
    int a = bot_score(offer->u.draft_offer.card[0]), b = bot_score(offer->u.draft_offer.card[1]);
    int idx = a >= b ? 0 : 1;
    if (bxs(rng) % 4 == 0) idx ^= 1;
    int sc = bot_score(offer->u.draft_offer.card[idx]);
    const uint8_t *l = offer->u.draft_offer.left; int mult;
    if (l[2] && sc >= 9) mult = 3; else if (l[1] && sc >= 6) mult = 2; else if (l[0]) mult = 1; else if (l[1]) mult = 2; else mult = 3;
    memset(out, 0, sizeof *out); out->type = DW_C_DRAFT_PICK; out->u.draft_pick.index = (uint8_t)idx; out->u.draft_pick.mult = (uint8_t)mult;
}

int dwc_run(DwClient *c, DwRunOpts *o) {
    uint32_t drng = o->seed * 2654435761u ^ 0xA5A5A5A5u; if (!drng) drng = 1;
    DwMsg m; DwPolicyCtx ctx; uint32_t match_id = 0; int8_t last_hand[4] = {-1, -1, -1, -1};
    dw_policy_reset(&ctx, o->seed);
    ctx.decide = o->decide; ctx.brain = o->brain; ctx.arch = o->arch; ctx.w_h = o->w_h; ctx.w_n = o->w_n; ctx.sigma = o->sigma;
    memset(&m, 0, sizeof m);
    m.type = DW_C_HELLO; m.u.hello.proto = DW_PROTO_VERSION; m.u.hello.mode = (uint8_t)(o->mode == DW_MODE_DRAFT ? DW_MODE_DRAFT : DW_MODE_CARD); m.u.hello.kind = (uint8_t)o->kind;
    strncpy(m.u.hello.name, o->name, DW_NAME_LEN);
    if (dwc_send(c, &m)) return -1;
    if (o->token && *o->token) {
        size_t tl = strlen(o->token);
        if (tl > DW_MAX_AUTH_TOKEN) { fprintf(stderr, "dwc: IDUNA token too long for AUTH (%zu > %d)\n", tl, DW_MAX_AUTH_TOKEN); return -2; }
        DwMsg a; memset(&a, 0, sizeof a); a.type = DW_C_AUTH; a.u.auth.token_len = (uint16_t)tl; memcpy(a.u.auth.token, o->token, tl);
        if (dwc_send(c, &a)) return -1;
    }
    int idle = 0;
    for (;;) {
        if (o->stop && *o->stop) return -3;
        int r = dwc_recv(c, &m, 500);
        if (r < 0) return -1;
        if (r == 0) { idle += 500; if (o->idle_timeout_ms > 0 && idle >= o->idle_timeout_ms) return -1; continue; }
        idle = 0;
        switch (m.type) {
        case DW_S_WELCOME: { DwMsg q; memset(&q, 0, sizeof q); q.type = DW_C_QUEUE; if (dwc_send(c, &q)) return -1; break; }
        case DW_S_QUEUED: break;
        case DW_S_DRAFT_OFFER: { DwMsg p; bot_draft_pick(&drng, &m, &p); if (dwc_send(c, &p)) return -1; break; }
        case DW_S_DRAFT_DONE: if (o->verbose) printf("deck %u drafted\n", m.u.draft_done.deck_id); break;
        case DW_S_MATCH_FOUND:
            match_id = m.u.match_found.match_id; dw_policy_reset(&ctx, o->seed ^ m.u.match_found.seed);
            if (o->verbose) printf("match %u vs %s (%s), seat %d\n", match_id, m.u.match_found.opp_name, m.u.match_found.opp_kind ? "bot" : "human", m.u.match_found.seat);
            break;
        case DW_S_ROUND_START: {
            ctx.hull_you = m.u.round_start.hull_you; ctx.hull_opp = m.u.round_start.hull_opp;
            ctx.energy_opp = m.u.round_start.energy_opp == DW_HIDDEN_U8 ? 0 : m.u.round_start.energy_opp; ctx.opp_hand_size = m.u.round_start.opp_hand_size;
            ctx.vault_you = m.u.round_start.vault_you; ctx.lock_mask = m.u.round_start.lock_mask;
            int slot = dw_policy_choose(o->policy, &ctx, m.u.round_start.hand, m.u.round_start.energy_you, m.u.round_start.round);
            memcpy(last_hand, m.u.round_start.hand, 4);
            if (o->think_ms > 0) dw_sleep_ms((unsigned)o->think_ms);
            if (o->verbose) printf("round %d: hull %d/%d energy %d/%d -> play slot %d\n", m.u.round_start.round, m.u.round_start.hull_you, m.u.round_start.hull_opp, m.u.round_start.energy_you, m.u.round_start.energy_opp, slot);
            DwMsg p; memset(&p, 0, sizeof p); p.type = DW_C_PLAY; p.u.play.match_id = match_id; p.u.play.round = m.u.round_start.round; p.u.play.slot = (int8_t)slot;
            if (dwc_send(c, &p)) return -1;
            break;
        }
        case DW_S_PLAY_ACK: break;
        case DW_S_PLAY_REJECT: {
            /* Only a bad/illegal card is worth retrying (as a pass). Stale-round/no-match/already-locked rejects
             * must NOT be answered, or a late client would ping-pong rejects with the server forever. */
            if (o->verbose) printf("play rejected (reason %d)\n", m.u.reject.reason);
            if (m.u.reject.reason == DW_REJ_ILLEGAL_CARD || m.u.reject.reason == DW_REJ_BAD_SLOT) {
                DwMsg p; memset(&p, 0, sizeof p); p.type = DW_C_PLAY; p.u.play.match_id = m.u.reject.match_id; p.u.play.round = m.u.reject.round; p.u.play.slot = -1;
                if (dwc_send(c, &p)) return -1;
            }
            break;
        }
        case DW_S_ROUND_RESULT:
            ctx.last_opp_card = m.u.round_result.card_opp;
            ctx.opp_kinds[0] = ctx.opp_kinds[1]; ctx.opp_kinds[1] = ctx.opp_kinds[2];
            ctx.opp_kinds[2] = m.u.round_result.card_opp >= 0 ? card_kind(m.u.round_result.card_opp) : 3;
            if (o->verbose) printf("  result: you %d opp %d, dmg taken %d dealt %d, hull %d/%d\n", m.u.round_result.card_you, m.u.round_result.card_opp, m.u.round_result.dmg_you, m.u.round_result.dmg_opp, m.u.round_result.hull_you, m.u.round_result.hull_opp);
            break;
        case DW_S_MATCH_END:
            o->done++;
            if (m.u.match_end.result == DW_RES_WIN) o->wins++; else if (m.u.match_end.result == DW_RES_LOSS) o->losses++; else o->draws++;
            if (o->verbose) printf("match end: %s (reason %d)\n", m.u.match_end.result == DW_RES_WIN ? "WIN" : m.u.match_end.result == DW_RES_LOSS ? "LOSS" : "DRAW", m.u.match_end.reason);
            if (o->target_matches > 0 && o->done >= o->target_matches) return 0;
            { DwMsg q; memset(&q, 0, sizeof q); q.type = DW_C_QUEUE;
              if (o->mode == DW_MODE_DRAFT) q.u.queue.same_deck = m.u.match_end.result != DW_RES_LOSS;   /* keep a winning deck, redraft after a loss */
              if (dwc_send(c, &q)) return -1; }
            break;
        case DW_S_ERROR: if (o->verbose) printf("server error %d\n", m.u.error.code); return -2;
        default: break;
        }
    }
    (void)last_hand;
}
