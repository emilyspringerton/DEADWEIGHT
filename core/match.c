#include "match.h"
#include "card_rules.h"
#include "protocol.h"
#include <string.h>

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void shuffle_pile(DwMatch *m, int s, int8_t *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(xs(&m->rng[s]) % (uint32_t)(i + 1));
        int8_t t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

/* Draw the top card for seat s; reshuffle the discard into the draw pile when empty. -1 if no cards remain. */
static int8_t draw_card(DwMatch *m, int s) {
    if (m->draw_n[s] == 0 && m->discard_n[s] > 0) {
        memcpy(m->draw[s], m->discard[s], (size_t)m->discard_n[s]);
        m->draw_n[s] = m->discard_n[s]; m->discard_n[s] = 0;
        shuffle_pile(m, s, m->draw[s], m->draw_n[s]);
    }
    return m->draw_n[s] ? m->draw[s][--m->draw_n[s]] : (int8_t)-1;
}

static void to_discard(DwMatch *m, int s, int8_t card) { if (card >= 0 && m->discard_n[s] < DW_DECK) m->discard[s][m->discard_n[s]++] = card; }

void dw_match_init(DwMatch *m, uint32_t seed) { dw_match_init_decks(m, seed, NULL, 0, NULL, 0); }

void dw_match_init_decks(DwMatch *m, uint32_t seed, const int8_t *deck0, int n0, const int8_t *deck1, int n1) {
    memset(m, 0, sizeof *m);
    m->seed = seed;
    for (int s = 0; s < 2; s++) {
        uint32_t r = seed * 2654435761u ^ (uint32_t)(s + 1) * 0x9E3779B9u;
        if (r == 0) r = 1;
        m->rng[s] = r;
        for (int i = 0; i < 4; i++) xs(&m->rng[s]);
        uint32_t f = seed * 2246822519u ^ (uint32_t)(s + 1) * 0xC2B2AE3Du;
        m->rfx[s] = f ? f : 1;
        for (int i = 0; i < 3; i++) xs(&m->rfx[s]);
        const int8_t *dk = s == 0 ? deck0 : deck1; int dn = s == 0 ? n0 : n1;
        if (!dk) { dn = num_cards(); }
        if (dn > DW_DECK) dn = DW_DECK;
        for (int i = 0; i < dn; i++) m->draw[s][i] = dk ? dk[i] : (int8_t)i;
        m->draw_n[s] = dn;
        shuffle_pile(m, s, m->draw[s], m->draw_n[s]);
        for (int i = 0; i < hand_size(); i++) m->hand[s][i] = draw_card(m, s);
        m->hull[s] = m->hull_start[s] = m->hull_prev[s] = (int8_t)start_hull();
        m->energy[s] = (uint8_t)start_energy();
        m->vault[s] = (int8_t)start_vault();
        m->lock_slot[s] = -1;
    }
}

void dw_match_begin_round(DwMatch *m) {
    m->round++;
    for (int s = 0; s < 2; s++) {
        m->hull_prev[s] = m->hull_start[s]; m->hull_start[s] = m->hull[s];
        int e = round_start_energy(m->energy[s]) + m->next_e[s];
        m->energy[s] = (uint8_t)clampi(e, 0, energy_cap(99)); m->next_e[s] = 0;
        m->vault[s] = (int8_t)clampi(round_start_vault(m->vault[s]), -100, 99);
        m->lock_mask[s] = m->next_lock[s]; m->next_lock[s] = 0;
        m->roll[s] = (uint8_t)(xs(&m->rfx[s]) % 100u);
        m->locked[s] = 0; m->lock_slot[s] = -1;
    }
}

int dw_match_lock(DwMatch *m, int seat, int slot) {
    if (m->done || m->round < 1) return DW_REJ_NO_MATCH;
    if (m->locked[seat]) return DW_REJ_ALREADY_LOCKED;
    if (slot < -1 || slot >= DW_HAND) return DW_REJ_BAD_SLOT;
    if (slot >= 0) {
        int id = m->hand[seat][slot];
        if (id < 0) return DW_REJ_BAD_SLOT;
        if ((m->lock_mask[seat] >> slot) & 1) return DW_REJ_ILLEGAL_CARD;
        if (!is_legal_play(id, m->energy[seat], m->vault[seat])) return DW_REJ_ILLEGAL_CARD;
    }
    m->locked[seat] = 1; m->lock_slot[seat] = (int8_t)slot;
    return 0;
}

int dw_match_both_locked(const DwMatch *m) { return m->locked[0] && m->locked[1]; }

/* Evaluate one effect word for seat s against the current meters. dealt/taken = pre-armor damage exchange so far. */
static int fxamt(const DwMatch *m, int s, int word, int dealt, int taken, const int eff[2]) {
    int o = 1 - s;
    int mk = card_kind(eff[s]);
    int ok = eff[o] >= 0 ? card_kind(eff[o]) : -1;
    int oc = eff[o] >= 0 ? card_cost(eff[o]) : 0;
    return fx_amount(word, dealt, taken, mk, ok, oc, m->hull[s], m->hull[o], m->energy[s], m->energy[o],
                     m->vault[s], m->vault[o], m->round, m->roll[s]);
}

typedef struct {
    int heal, selfdmg, truedmg, energy, oenergy, nexte, nextl, onextl, armor, vault, spend, ospend, burn, regen, lock, blind,
        swap, redraw, discard, equalize, chrono, convert, cleanse, debtfree;
} Acc;

static void add_hull(DwMatch *m, int s, int delta) { m->hull[s] = (int8_t)clampi(m->hull[s] + delta, -100, start_hull()); }

void dw_match_resolve(DwMatch *m, DwOutcome *out) {
    DwOutcome o; memset(&o, 0, sizeof o);
    int decl[2], eff[2], orig[2], canc[2] = {0, 0}, copied[2] = {0, 0};
    for (int s = 0; s < 2; s++) {
        decl[s] = m->lock_slot[s] >= 0 ? m->hand[s][m->lock_slot[s]] : -1;
        eff[s] = orig[s] = decl[s] >= 0 ? card_substitute(decl[s], m->roll[s]) : -1;
        /* costs are paid for the card actually declared, even if it is later cancelled */
        m->energy[s] = (uint8_t)energy_after_play(m->energy[s], decl[s]);
        if (decl[s] >= 0) m->vault[s] = (int8_t)clampi(m->vault[s] - card_credit(decl[s]), -100, 99);
    }
    /* 1. cancel, then copy (both read the cards as declared, so mutual cancels cancel each other) */
    for (int s = 0; s < 2; s++) {
        if (orig[s] < 0) continue;
        int ws[2] = { card_fx_a(orig[s]), card_fx_b(orig[s]) };
        for (int k = 0; k < 2; k++)
            if (fx_ch(ws[k]) == DW_CH_CANCEL && fxamt(m, s, ws[k], 0, 0, orig) > 0) canc[1 - s] = 1;
    }
    for (int s = 0; s < 2; s++) if (canc[s]) eff[s] = -1;
    for (int s = 0; s < 2; s++) {
        if (canc[s] || orig[s] < 0) continue;
        int ws[2] = { card_fx_a(orig[s]), card_fx_b(orig[s]) };
        for (int k = 0; k < 2; k++)
            if (fx_ch(ws[k]) == DW_CH_COPY && fxamt(m, s, ws[k], 0, 0, orig) > 0) { eff[s] = orig[1 - s]; copied[s] = 1; }
    }
    /* 2. triangle damage, then phase-1 riders (channels < 20) */
    int d[2] = { damage_dealt(eff[0], eff[1]), damage_dealt(eff[1], eff[0]) };
    int countered[2];
    for (int s = 0; s < 2; s++) countered[s] = eff[s] >= 0 && eff[1 - s] >= 0 && kind_beats(card_kind(eff[1 - s]), card_kind(eff[s]));
    int bonus[2] = {0, 0}, pierce[2] = {0, 0}, bypass[2] = {0, 0}, immune[2] = {0, 0}, life[2] = {0, 0}, refl[2] = {0, 0}, halve = 0;
    for (int s = 0; s < 2; s++) {           /* pass A: BYPASS first, since it rewrites the opponent's counter damage */
        if (eff[s] < 0) continue;
        int ws[2] = { card_fx_a(eff[s]), card_fx_b(eff[s]) };
        for (int k = 0; k < 2; k++) if (fx_ch(ws[k]) == DW_CH_BYPASS) {
            int a = fxamt(m, s, ws[k], d[s], d[1 - s], eff);
            if (a > bypass[s]) bypass[s] = a;
            if (a > 0 && eff[1 - s] >= 0 && card_kind(eff[1 - s]) == 2) d[1 - s] = 0;
        }
    }
    for (int s = 0; s < 2; s++) {           /* pass B */
        if (eff[s] < 0) continue;
        int ws[2] = { card_fx_a(eff[s]), card_fx_b(eff[s]) };
        for (int k = 0; k < 2; k++) {
            int ch = fx_ch(ws[k]);
            if (ch != DW_CH_BONUS && ch != DW_CH_PIERCE && ch != DW_CH_IMMUNE && ch != DW_CH_LIFELINE && ch != DW_CH_REFLECT && ch != DW_CH_HALVE) continue;
            int a = fxamt(m, s, ws[k], d[s], d[1 - s], eff);
            if (a <= 0) continue;
            switch (ch) {
            case DW_CH_BONUS: bonus[s] += a; break;
            case DW_CH_PIERCE: pierce[s] = 1; break;
            case DW_CH_IMMUNE: immune[s] = 1; break;
            case DW_CH_LIFELINE: life[s] = 1; break;
            case DW_CH_REFLECT: refl[s] += a; break;
            case DW_CH_HALVE: halve = 1; break;
            }
        }
    }
    int outd[2];
    for (int s = 0; s < 2; s++) {
        outd[s] = 0;
        if (eff[s] < 0) continue;
        outd[s] = d[s];
        if (!countered[s]) outd[s] += bonus[s];
        if (bypass[s] > outd[s]) outd[s] = bypass[s];
        outd[s] += refl[s];
    }
    if (halve) { outd[0] /= 2; outd[1] /= 2; }
    for (int s = 0; s < 2; s++) if (immune[s]) outd[1 - s] = 0;
    int in[2] = { outd[1], outd[0] };
    /* 3. attack damage: armor soaks first unless the attacker pierces */
    int hdmg[2] = {0, 0}, healed[2] = {0, 0};
    for (int s = 0; s < 2; s++) {
        int soak = 0;
        if (in[s] > 0 && !pierce[1 - s]) { soak = m->armor[s] < in[s] ? m->armor[s] : in[s]; m->armor[s] = (uint8_t)(m->armor[s] - soak); }
        hdmg[s] += in[s] - soak; add_hull(m, s, -(in[s] - soak));
    }
    /* 4. phase-2 riders: collect everything from the post-attack state, then apply in a fixed order */
    Acc a[2]; memset(a, 0, sizeof a);
    for (int s = 0; s < 2; s++) {
        if (eff[s] < 0) continue;
        int ws[2] = { card_fx_a(eff[s]), card_fx_b(eff[s]) };
        for (int k = 0; k < 2; k++) {
            int ch = fx_ch(ws[k]);
            if (ch < 20) continue;
            int amt = fxamt(m, s, ws[k], outd[s], in[s], eff);
            if (amt <= 0) continue;
            switch (ch) {
            case DW_CH_HEAL: a[s].heal += amt; break;         case DW_CH_SELFDMG: a[s].selfdmg += amt; break;
            case DW_CH_TRUE: a[s].truedmg += amt; break;      case DW_CH_ENERGY: a[s].energy += amt; break;
            case DW_CH_OENERGY: a[s].oenergy += amt; break;   case DW_CH_NEXTE: a[s].nexte += amt; break;
            case DW_CH_NEXTL: a[s].nextl += amt; break;       case DW_CH_ONEXTL: a[s].onextl += amt; break;
            case DW_CH_ARMOR: a[s].armor += amt; break;       case DW_CH_VAULT: a[s].vault += amt; break;
            case DW_CH_SPEND: a[s].spend += amt; break;       case DW_CH_OSPEND: a[s].ospend += amt; break;
            case DW_CH_BURN: a[s].burn = amt; break;          case DW_CH_REGEN: a[s].regen = amt; break;
            case DW_CH_DISABLE: a[s].lock += amt; break;         case DW_CH_BLIND: if (amt > a[s].blind) a[s].blind = amt; break;
            case DW_CH_SWAP: a[s].swap = 1; break;            case DW_CH_REDRAW: a[s].redraw = 1; break;
            case DW_CH_DISCARD: a[s].discard += amt; break;   case DW_CH_EQUALIZE: a[s].equalize = 1; break;
            case DW_CH_CHRONO: a[s].chrono = 1; break;        case DW_CH_CONVERT: a[s].convert += amt; break;
            case DW_CH_CLEANSE: a[s].cleanse = 1; break;      case DW_CH_DEBTFREE: a[s].debtfree = 1; break;
            }
        }
    }
    int flags[2] = {0, 0}, hull_before_fx[2] = { m->hull[0], m->hull[1] };
    for (int s = 0; s < 2; s++) { if (canc[s]) flags[s] |= DW_FX_CANCELLED; if (copied[s]) flags[s] |= DW_FX_COPIED; if (immune[s]) flags[s] |= DW_FX_IMMUNE; }
    for (int s = 0; s < 2; s++) {
        int t = 1 - s;
        if (a[s].selfdmg) { hdmg[s] += a[s].selfdmg; add_hull(m, s, -a[s].selfdmg); }
        if (a[s].truedmg) { hdmg[t] += a[s].truedmg; add_hull(m, t, -a[s].truedmg); }
    }
    for (int s = 0; s < 2; s++) {
        int t = 1 - s;
        if (a[s].heal && m->hull[s] < start_hull()) { int h = clampi(a[s].heal, 0, start_hull() - m->hull[s]); healed[s] += h; add_hull(m, s, h); }
        m->armor[s] = (uint8_t)clampi(m->armor[s] + a[s].armor, 0, DW_ARMOR_MAX);
        if (a[s].convert) { int c = clampi(a[s].convert, 0, m->energy[s]); m->energy[s] = (uint8_t)(m->energy[s] - c); m->armor[s] = (uint8_t)clampi(m->armor[s] + c, 0, DW_ARMOR_MAX); }
        m->energy[s] = (uint8_t)clampi(m->energy[s] + a[s].energy, 0, energy_cap(99));
        m->energy[t] = (uint8_t)clampi(m->energy[t] - a[s].oenergy, 0, energy_cap(99));
        m->next_e[s] = (int8_t)clampi(m->next_e[s] + a[s].nexte - a[s].nextl, -20, 20);
        m->next_e[t] = (int8_t)clampi(m->next_e[t] - a[s].onextl, -20, 20);
        m->vault[s] = (int8_t)clampi(m->vault[s] + a[s].vault - a[s].spend, -100, 99);
        m->vault[t] = (int8_t)clampi(m->vault[t] - a[s].ospend, -100, 99);
        if (a[s].cleanse) m->burn_left[s] = 0;
        if (a[s].debtfree && m->vault[s] < 0) m->vault[s] = 0;
    }
    if (a[0].equalize || a[1].equalize) {
        int avg = (m->hull[0] + m->hull[1] + 1) / 2;
        for (int s = 0; s < 2; s++) { int dlt = avg - m->hull[s]; if (dlt < 0) hdmg[s] += -dlt; else healed[s] += dlt; m->hull[s] = (int8_t)clampi(avg, -100, start_hull()); }
    }
    for (int s = 0; s < 2; s++) if (a[s].chrono && m->hull_prev[s] > m->hull[s]) { healed[s] += m->hull_prev[s] - m->hull[s]; m->hull[s] = m->hull_prev[s]; }
    for (int s = 0; s < 2; s++) if (life[s] && m->hull[s] <= 0) { m->hull[s] = 1; flags[s] |= DW_FX_LIFELINE; }
    /* 5. burn/regen tick (statuses applied THIS round start ticking next round) */
    for (int s = 0; s < 2; s++) {
        if (m->regen_left[s]) { int h = clampi(m->regen_amt[s], 0, start_hull() - m->hull[s] > 0 ? start_hull() - m->hull[s] : 0); healed[s] += h; add_hull(m, s, h); m->regen_left[s]--; }
        if (m->burn_left[s]) { hdmg[s] += m->burn_amt[s]; add_hull(m, s, -m->burn_amt[s]); m->burn_left[s]--; }
        if (m->hidden[s]) m->hidden[s]--;
        if (life[s] && m->hull[s] <= 0) { m->hull[s] = 1; flags[s] |= DW_FX_LIFELINE; }
    }
    for (int s = 0; s < 2; s++) {
        int t = 1 - s;
        if (a[s].burn) { m->burn_amt[t] = (uint8_t)(a[s].burn / 16); m->burn_left[t] = (uint8_t)(a[s].burn % 16); }
        if (a[s].regen) { m->regen_amt[s] = (uint8_t)(a[s].regen / 16); m->regen_left[s] = (uint8_t)(a[s].regen % 16); }
        if (a[s].blind) m->hidden[s] = (uint8_t)a[s].blind;
    }
    (void)hull_before_fx;
    /* 6. played cards go to the discard pile and their slots refill, then hand effects */
    for (int s = 0; s < 2; s++) if (decl[s] >= 0) { to_discard(m, s, (int8_t)decl[s]); m->hand[s][m->lock_slot[s]] = draw_card(m, s); }
    for (int s = 0; s < 2; s++) if (a[s].redraw) {
        for (int i = 0; i < DW_HAND; i++) { to_discard(m, s, m->hand[s][i]); m->hand[s][i] = -1; }
        for (int i = 0; i < DW_HAND; i++) m->hand[s][i] = draw_card(m, s);
        flags[s] |= DW_FX_REDREW;
    }
    if (a[0].swap != a[1].swap) {
        int8_t t[DW_HAND]; memcpy(t, m->hand[0], DW_HAND); memcpy(m->hand[0], m->hand[1], DW_HAND); memcpy(m->hand[1], t, DW_HAND);
        flags[0] |= DW_FX_SWAPPED; flags[1] |= DW_FX_SWAPPED;
    }
    for (int s = 0; s < 2; s++) {
        int t = 1 - s;
        for (int k = 0; k < a[s].discard; k++) {
            int slot = (int)(xs(&m->rfx[s]) % DW_HAND);
            if (m->hand[t][slot] >= 0) { to_discard(m, t, m->hand[t][slot]); m->hand[t][slot] = draw_card(m, t); flags[s] |= DW_FX_DISCARDED_OPP; }
        }
        for (int k = 0; k < a[s].lock && k < DW_HAND; k++) {
            int slot = (int)(xs(&m->rfx[s]) % DW_HAND), tries = 0;
            while (((m->next_lock[t] >> slot) & 1) && tries++ < DW_HAND) slot = (slot + 1) % DW_HAND;
            m->next_lock[t] = (uint8_t)(m->next_lock[t] | (1u << slot)); flags[s] |= DW_FX_LOCKED_OPP;
        }
    }
    /* 7. end conditions */
    int bank[2], lose[2];
    for (int s = 0; s < 2; s++) { bank[s] = bankrupt(m->vault[s]); lose[s] = m->hull[s] <= 0 || bank[s]; }
    if (match_decided(m->hull[0], m->hull[1]) || bank[0] || bank[1]) {
        m->done = 1;
        m->reason = (m->hull[0] <= 0 || m->hull[1] <= 0) ? DW_END_HULL : DW_END_BANKRUPT;
        if (lose[0] && lose[1]) { m->result[0] = m->result[1] = DW_RES_DRAW; }
        else { m->result[0] = lose[0] ? DW_RES_LOSS : DW_RES_WIN; m->result[1] = lose[1] ? DW_RES_LOSS : DW_RES_WIN; }
    } else if (m->round >= max_rounds()) {
        int w = round_winner(m->hull[0], m->hull[1], m->vault[0], m->vault[1]);
        m->done = 1; m->reason = DW_END_ROUNDS;
        m->result[0] = w == 0 ? DW_RES_WIN : w == 1 ? DW_RES_LOSS : DW_RES_DRAW;
        m->result[1] = w == 1 ? DW_RES_WIN : w == 0 ? DW_RES_LOSS : DW_RES_DRAW;
    }
    for (int s = 0; s < 2; s++) {
        o.card[s] = (int8_t)decl[s]; o.eff[s] = (int8_t)eff[s];
        o.dmg_to[s] = (uint8_t)clampi(hdmg[s], 0, 255); o.heal[s] = (uint8_t)clampi(healed[s], 0, 255);
        o.roll[s] = m->roll[s]; o.flags[s] = (uint8_t)flags[s];
    }
    if (out) *out = o;
}

void dw_match_forfeit(DwMatch *m, int loser_seat) {
    if (m->done) return;
    m->done = 1; m->reason = DW_END_FORFEIT;
    m->result[loser_seat] = DW_RES_LOSS; m->result[1 - loser_seat] = DW_RES_WIN;
}

static uint8_t status_bits(const DwMatch *m, int s) {
    return (uint8_t)((m->burn_left[s] ? DW_ST_BURN : 0) | (m->regen_left[s] ? DW_ST_REGEN : 0) | (m->hidden[s] ? DW_ST_HIDDEN : 0));
}

void dw_match_view(const DwMatch *m, int seat, DwView *v) {
    int o = 1 - seat, n = 0;
    memset(v, 0, sizeof *v);
    v->round = m->round; v->hull_you = m->hull[seat]; v->hull_opp = m->hull[o];
    v->energy_you = m->energy[seat]; v->armor_you = m->armor[seat]; v->vault_you = m->vault[seat];
    if (m->hidden[o]) { v->energy_opp = DW_HIDDEN_U8; v->armor_opp = DW_HIDDEN_U8; v->vault_opp = DW_HIDDEN_I8; }
    else { v->energy_opp = m->energy[o]; v->armor_opp = m->armor[o]; v->vault_opp = m->vault[o]; }
    memcpy(v->hand, m->hand[seat], DW_HAND);
    for (int i = 0; i < DW_HAND; i++) if (m->hand[o][i] >= 0) n++;
    v->opp_hand_size = (uint8_t)n; v->lock_mask = m->lock_mask[seat];
    v->status_you = status_bits(m, seat); v->status_opp = status_bits(m, o);
}

void dw_match_post_view(const DwMatch *m, int seat, DwPostView *v) {
    int o = 1 - seat;
    v->armor_you = m->armor[seat]; v->vault_you = m->vault[seat];
    if (m->hidden[o]) { v->armor_opp = DW_HIDDEN_U8; v->vault_opp = DW_HIDDEN_I8; }
    else { v->armor_opp = m->armor[o]; v->vault_opp = m->vault[o]; }
}
