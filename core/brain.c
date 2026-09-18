#include "brain.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "card_rules.h"

/* --- PARENA-generated (core/bot_brain.c) --- */
Vec mlp_forward3(Vec *, Vec *, Vec *, Vec *, Vec *, Vec *, Vec *, int, int, int, int, Arena *);
double heuristic_prior(int arch, int card, int last_kind, int bank);
double blend(double prior, double logit, double w_h, double w_n);
double mask_score(double s, int legal);
int pick5(double, double, double, double, double);

static uint32_t rd_u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rd_f32(const uint8_t *p) { uint32_t u = rd_u32(p); float f; memcpy(&f, &u, 4); return f; }

int dwb_load_mem(DwBrain *out, const uint8_t *d, size_t len) {
    if (!d || len < 12 || memcmp(d, "BPMW", 4) != 0) return 0;
    if (rd_u32(d + 4) != 1 || rd_u32(d + 8) != 3) return 0;
    uint32_t in[3], od[3]; uint8_t act[3]; size_t cur = 12;
    for (int i = 0; i < 3; i++) {
        if (cur + 9 > len) return 0;
        in[i] = rd_u32(d + cur); od[i] = rd_u32(d + cur + 4); act[i] = d[cur + 8]; cur += 9;
    }
    if (in[0] != DW_OBS_DIM || od[2] != DW_N_ACTIONS || in[1] != od[0] || in[2] != od[1]) return 0;
    if (act[0] != 1 || act[1] != 1 || act[2] != 0) return 0;
    if (od[0] < 1 || od[0] > DW_BRAIN_MAX_HIDDEN || od[1] < 1 || od[1] > DW_BRAIN_MAX_HIDDEN) return 0;
    size_t need = cur;
    for (int i = 0; i < 3; i++) need += ((size_t)in[i] * od[i] + od[i]) * 4;   /* dims are bounded above: no overflow */
    if (need != len) return 0;                                                  /* exact size: truncated or trailing bytes rejected */
    for (size_t k = cur; k < len; k += 4) if (!isfinite(rd_f32(d + k))) return 0;

    DwBrain b; memset(&b, 0, sizeof b);
    arena_init(&b.arena);
    b.n[0] = (int)in[0]; b.n[1] = (int)od[0]; b.n[2] = (int)od[1]; b.n[3] = (int)od[2];
    for (int i = 0; i < 3; i++) {
        b.w[i] = vec_new(&b.arena); b.b[i] = vec_new(&b.arena);
        size_t nw = (size_t)in[i] * od[i];
        for (size_t k = 0; k < nw; k++, cur += 4) vec_push_(&b.w[i], vec_box_f64(&b.w[i], (double)rd_f32(d + cur)));
        for (uint32_t k = 0; k < od[i]; k++, cur += 4) vec_push_(&b.b[i], vec_box_f64(&b.b[i], (double)rd_f32(d + cur)));
    }
    b.loaded = 1;
    *out = b;   /* Vecs hold &b.arena of the LOCAL: re-point them at the caller's copy */
    for (int i = 0; i < 3; i++) { out->w[i].arena = &out->arena; out->b[i].arena = &out->arena; }
    return 1;
}

int dwb_load_file(DwBrain *b, const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f); if (sz <= 0 || sz > (1 << 20)) { fclose(f); return 0; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz); if (!buf) { fclose(f); return 0; }
    int ok = fread(buf, 1, (size_t)sz, f) == (size_t)sz && dwb_load_mem(b, buf, (size_t)sz);
    fclose(f); free(buf); return ok;
}

extern const unsigned char dw_brain_default_blob[];
extern const unsigned int dw_brain_default_blob_len;
int dwb_load_default(DwBrain *b) { return dwb_load_mem(b, dw_brain_default_blob, dw_brain_default_blob_len); }

void dwb_free(DwBrain *b) { if (b->loaded) { arena_free_all(&b->arena); memset(b, 0, sizeof *b); } }

/* Observation layout == training/dw_env.py build_observation():
 * [0..4] hull_you/20 hull_opp/20 energy_you/6 energy_opp/6 round/8; [5..44] 4 hand slots x 10 one-hot (card 0..8, 9 = empty);
 * [45] opp_hand_size/4; [46..57] opp's last 3 kinds (oldest first) x 4 one-hot (BURST, TANK, SHIELD, none/pass). */
void dwb_build_obs(double o[DW_OBS_DIM], const DwPolicyCtx *c, const int8_t hand[4], int energy_you, int round) {
    memset(o, 0, sizeof(double) * DW_OBS_DIM);
    o[0] = c->hull_you / 20.0; o[1] = c->hull_opp / 20.0; o[2] = energy_you / 6.0; o[3] = c->energy_opp / 6.0; o[4] = round / 8.0;
    for (int s = 0; s < 4; s++) { int id = hand[s]; o[5 + s * 10 + (id >= 0 && id <= 8 ? id : 9)] = 1.0; }
    o[45] = c->opp_hand_size / 4.0;
    for (int k = 0; k < 3; k++) { int kd = c->opp_kinds[k]; o[46 + k * 4 + (kd >= 0 && kd <= 2 ? kd : 3)] = 1.0; }
}

int dwb_logits(const DwBrain *b, const double obs[DW_OBS_DIM], double out[DW_N_ACTIONS]) {
    if (!b || !b->loaded) return 0;
    Arena tmp; arena_init(&tmp);
    Vec x = vec_new(&tmp);
    for (int i = 0; i < DW_OBS_DIM; i++) vec_push_(&x, vec_box_f64(&x, obs[i]));
    /* PARENA takes non-const Vec*; it only reads the weights. */
    Vec r = mlp_forward3((Vec *)&b->w[0], (Vec *)&b->b[0], (Vec *)&b->w[1], (Vec *)&b->b[1], (Vec *)&b->w[2], (Vec *)&b->b[2],
                         &x, b->n[0], b->n[1], b->n[2], b->n[3], &tmp);
    int ok = vec_len(&r) == DW_N_ACTIONS;
    if (ok) for (int i = 0; i < DW_N_ACTIONS; i++) out[i] = *(double *)vec_get(&r, i);
    arena_free_all(&tmp);
    return ok;
}

/* ~N(0,1) (Irwin-Hall, 4 uniforms) from the seeded per-match RNG: deterministic given the match seed. */
static double gauss(DwPolicyCtx *c) {
    double s = 0; for (int i = 0; i < 4; i++) s += (dw_policy_rand(c) >> 8) / 16777216.0;
    return (s - 2.0) * 1.7320508075688772;
}

int dwb_choose(DwPolicyCtx *c, const int8_t hand[4], int energy, int round) {
    double logits[DW_N_ACTIONS] = {0, 0, 0, 0, 0}, w_n = c->w_n;
    if (c->brain && w_n != 0.0) {
        double obs[DW_OBS_DIM]; dwb_build_obs(obs, c, hand, energy, round);
        if (!dwb_logits(c->brain, obs, logits)) { memset(logits, 0, sizeof logits); w_n = 0.0; }
    } else w_n = 0.0;
    int last_kind = c->last_opp_card >= 0 ? card_kind(c->last_opp_card) : -1;
    int bank = 0;
    for (int i = 0; i < 4; i++) if (hand[i] == 2 && energy < card_cost(2)) bank = 1;
    double m[5];
    for (int i = 0; i < 5; i++) {
        int card = i < 4 ? hand[i] : -1;
        int legal = i == 4 ? 1 : (card >= 0 && is_legal_play(card, energy));
        double s = blend(heuristic_prior(c->arch, card, last_kind, bank), logits[i], c->w_h, w_n);
        if (c->sigma > 0.0) s += c->sigma * gauss(c);
        m[i] = mask_score(s, legal);
    }
    int slot = pick5(m[0], m[1], m[2], m[3], m[4]);
    /* defence in depth: the mask already guarantees legality, but a bug here must never put an illegal play on the wire */
    if (slot >= 0 && !(hand[slot] >= 0 && is_legal_play(hand[slot], energy))) return -1;
    return slot;
}
