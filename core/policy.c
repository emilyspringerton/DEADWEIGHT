#include "policy.h"
#include "card_rules.h"
#include <stdio.h>
#include <string.h>

static uint32_t xs(DwPolicyCtx *c) { uint32_t x = c->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return c->rng = x; }

int dw_policy_from_name(const char *n) {
    if (!strcmp(n, "random")) return DW_POL_RANDOM;
    if (!strcmp(n, "first-legal")) return DW_POL_FIRST_LEGAL;
    if (!strcmp(n, "ripper")) return DW_POL_RIPPER;
    if (!strcmp(n, "wall")) return DW_POL_WALL;
    if (!strcmp(n, "mirror")) return DW_POL_MIRROR;
    if (!strcmp(n, "interactive")) return DW_POL_INTERACTIVE;
    return -1;
}

uint32_t dw_policy_rand(DwPolicyCtx *c) { return xs(c); }

/* Resets per-match state; brain configuration (brain/arch/w_h/w_n/sigma) is deliberately left alone. */
void dw_policy_reset(DwPolicyCtx *c, uint32_t seed) {
    c->rng = seed ? seed : 0x2545F491u; c->last_opp_card = -1;
    c->hull_you = c->hull_opp = start_hull(); c->energy_opp = start_energy(); c->opp_hand_size = hand_size();
    c->opp_kinds[0] = c->opp_kinds[1] = c->opp_kinds[2] = 3;
}

static int legal(const int8_t hand[4], int i, int energy) { return hand[i] >= 0 && is_legal_play(hand[i], energy); }

/* best legal slot of `kind` by power (ties: lower slot); kind < 0 = any kind. -1 if none. */
static int best_of_kind(const int8_t hand[4], int energy, int kind) {
    int best = -1;
    for (int i = 0; i < 4; i++)
        if (legal(hand, i, energy) && (kind < 0 || card_kind(hand[i]) == kind) && (best < 0 || card_power(hand[i]) > card_power(hand[best]))) best = i;
    return best;
}

static int interactive(const int8_t hand[4], int energy, int round) {
    for (;;) {
        printf("round %d energy %d hand:", round, energy);
        for (int i = 0; i < 4; i++) printf(" [%d]=%d%s", i, hand[i], legal(hand, i, energy) ? "" : "x");
        printf("  play slot 0-3, or p to pass> "); fflush(stdout);
        char line[64];
        if (!fgets(line, sizeof line, stdin)) return -1;
        if (line[0] == 'p' || line[0] == 'P') return -1;
        if (line[0] >= '0' && line[0] <= '3' && legal(hand, line[0] - '0', energy)) return line[0] - '0';
        printf("not a legal play\n");
    }
}

int dw_policy_choose(int policy, DwPolicyCtx *c, const int8_t hand[4], int energy, int round) {
    int opts[5], n = 0, i;
    switch (policy) {
    case DW_POL_INTERACTIVE: return interactive(hand, energy, round);
    case DW_POL_HYBRID: return c->decide ? c->decide(c, hand, energy, round) : -1;
    case DW_POL_FIRST_LEGAL:
        for (i = 0; i < 4; i++) if (legal(hand, i, energy)) return i;
        return -1;
    case DW_POL_RIPPER: {
        /* Burst-heavy. Banks energy for a tier-2 Burst (cost 4) when holding one it can't yet afford. */
        int holds_big = 0;
        for (i = 0; i < 4; i++) if (hand[i] == 2) holds_big = 1;
        if (holds_big && energy < card_cost(2)) return -1;
        int b = best_of_kind(hand, energy, 0);
        return b >= 0 ? b : best_of_kind(hand, energy, -1);
    }
    case DW_POL_WALL: {
        /* Tank-heavy, Shield as the answer to Burst-happy openers, Burst only as a last resort. */
        int t = best_of_kind(hand, energy, 1);
        if (t >= 0) return t;
        int s = best_of_kind(hand, energy, 2);
        return s >= 0 ? s : best_of_kind(hand, energy, 0);
    }
    case DW_POL_MIRROR: {
        /* Predict the opponent repeats their last kind; answer with the kind that beats it. */
        if (c->last_opp_card >= 0) {
            int k = card_kind(c->last_opp_card);
            for (int cand = 0; cand < 3; cand++) if (kind_beats(cand, k)) {
                int b = best_of_kind(hand, energy, cand);
                if (b >= 0) return b;
            }
        }
        /* fall through to random when there's nothing to mirror or no counter card affordable */
    }
    /* fallthrough */
    case DW_POL_RANDOM:
    default:
        opts[n++] = -1;
        for (i = 0; i < 4; i++) if (legal(hand, i, energy)) opts[n++] = i;
        return opts[xs(c) % (uint32_t)n];
    }
}
