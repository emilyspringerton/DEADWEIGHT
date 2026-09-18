/* Hybrid brain tests (S503-14b), run under ASan+UBSan. Hand-typed oracles where possible so the generated PARENA
 * code can't grade itself; cross-language forward-pass parity is training/test_dw_brain.py (uses tests/brain_dump.c). */
#include "brain.h"   /* first: pulls parena_runtime.h before any system header */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "card_rules.h"

static int checks = 0, fails = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

extern const unsigned char dw_brain_default_blob[]; extern const unsigned int dw_brain_default_blob_len;

static uint32_t rs = 12345;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void putf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); put32(p, u); }

/* Build a 58 -> 1 -> 1 -> 5 blob: layer0 all-zero (W=0, b=0) => h1 = 0; layer1 W=0, b1 => h2 = tanh(b1); layer2 W=w2, b=b2. */
static size_t tiny_blob(uint8_t *buf, float b1, const float w2[5], const float b2[5]) {
    size_t c = 0; memcpy(buf, "BPMW", 4); put32(buf + 4, 1); put32(buf + 8, 3); c = 12;
    uint32_t dims[3][3] = {{58, 1, 1}, {1, 1, 1}, {1, 5, 0}};
    for (int i = 0; i < 3; i++) { put32(buf + c, dims[i][0]); put32(buf + c + 4, dims[i][1]); buf[c + 8] = (uint8_t)dims[i][2]; c += 9; }
    for (int i = 0; i < 58; i++) { putf(buf + c, 0.f); c += 4; } putf(buf + c, 0.f); c += 4;   /* layer 0: W (1x58), b */
    putf(buf + c, 0.f); c += 4; putf(buf + c, b1); c += 4;                                      /* layer 1: W (1x1), b */
    for (int i = 0; i < 5; i++) { putf(buf + c, w2[i]); c += 4; } for (int i = 0; i < 5; i++) { putf(buf + c, b2[i]); c += 4; }
    return c;
}

static DwPolicyCtx mkctx(void) { DwPolicyCtx c; memset(&c, 0, sizeof c); dw_policy_reset(&c, 7); return c; }

static void rand_state(DwPolicyCtx *c, int8_t hand[4], int *energy, int *round) {
    for (int i = 0; i < 4; i++) hand[i] = (int8_t)((int)(rnd() % 11) - 1 > 8 ? -1 : (int)(rnd() % 11) - 1);
    *energy = (int)(rnd() % 7); *round = 1 + (int)(rnd() % 8);
    c->hull_you = 1 + (int)(rnd() % 20); c->hull_opp = 1 + (int)(rnd() % 20); c->energy_opp = (int)(rnd() % 7); c->opp_hand_size = 1 + (int)(rnd() % 4);
    for (int k = 0; k < 3; k++) c->opp_kinds[k] = (int)(rnd() % 4);
    c->last_opp_card = (int)(rnd() % 10) - 1;
}

int main(void) {
    /* 1. hand-oracle forward pass: out_i = w2_i * tanh(0.5) + b2_i (tanh via the PARENA rational approximation) */
    { uint8_t buf[512]; float w2[5] = {1, 2, 3, -1, 0.5f}, b2[5] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
      size_t n = tiny_blob(buf, 0.5f, w2, b2); DwBrain b; CHECK(dwb_load_mem(&b, buf, n), "tiny blob loads");
      double obs[DW_OBS_DIM] = {0}, out[5]; CHECK(dwb_logits(&b, obs, out), "forward");
      for (int i = 0; i < 5; i++) CHECK(fabs(out[i] - (w2[i] * tanh(0.5) + b2[i])) < 1e-5, "logit %d = %.9f", i, out[i]);
      dwb_free(&b); }

    /* 2. default weights: load, determinism, finiteness */
    DwBrain def; CHECK(dwb_load_default(&def), "default weights load");
    { DwPolicyCtx c = mkctx(); int8_t h[4] = {2, 4, 6, 0}; double o[DW_OBS_DIM], a[5], b2[5];
      c.hull_you = 15; c.hull_opp = 9; c.energy_opp = 3; c.opp_hand_size = 4; c.opp_kinds[2] = 1;
      dwb_build_obs(o, &c, h, 4, 3); CHECK(dwb_logits(&def, o, a) && dwb_logits(&def, o, b2), "logits");
      CHECK(memcmp(a, b2, sizeof a) == 0, "forward pass deterministic (bitwise)");
      for (int i = 0; i < 5; i++) CHECK(isfinite(a[i]) && fabs(a[i]) < 10, "logit %d finite/bounded: %g", i, a[i]);
      /* obs layout oracle (docs in core/brain.h == training/dw_env.py): hull, hand one-hots, opp kind history */
      CHECK(o[0] == 15 / 20.0 && o[1] == 9 / 20.0 && o[2] == 4 / 6.0 && o[3] == 3 / 6.0 && o[4] == 3 / 8.0, "scalar obs slots");
      CHECK(o[5 + 0 * 10 + 2] == 1.0 && o[5 + 1 * 10 + 4] == 1.0 && o[5 + 2 * 10 + 6] == 1.0 && o[5 + 3 * 10 + 0] == 1.0, "hand one-hots");
      CHECK(o[45] == 1.0 && o[46 + 0 * 4 + 3] == 1.0 && o[46 + 1 * 4 + 3] == 1.0 && o[46 + 2 * 4 + 1] == 1.0, "opp hand size + kind history");
      double sum = 0; for (int i = 5; i < 45; i++) sum += o[i]; CHECK(sum == 4.0, "exactly one hot per hand slot"); }

    /* 3. the mask: 10k random states x random config never yields an illegal play (ASan/UBSan active) */
    { long illegal = 0, passes = 0, plays = 0;
      for (int it = 0; it < 10000; it++) {
          DwPolicyCtx c = mkctx(); int8_t hand[4]; int energy, round; rand_state(&c, hand, &energy, &round);
          c.brain = &def; c.arch = (int)(rnd() % 3); c.w_h = (rnd() % 3) * 0.5; c.w_n = (rnd() % 4) * 0.7; c.sigma = (rnd() % 3) * 0.5; c.rng = rnd() | 1;
          int s = dwb_choose(&c, hand, energy, round);
          if (s == -1) passes++; else { plays++; if (s < 0 || s > 3 || hand[s] < 0 || !is_legal_play(hand[s], energy)) illegal++; }
      }
      CHECK(illegal == 0, "%ld illegal plays", illegal); CHECK(passes > 0 && plays > 0, "both pass (%ld) and play (%ld) occur", passes, plays); }

    /* 4. hybrid with w_n = 0, sigma = 0 reproduces the heuristic archetype's own choice (ripper/wall) on random states */
    { long same = 0, tot = 0;
      for (int it = 0; it < 5000; it++) {
          DwPolicyCtx c = mkctx(); int8_t hand[4]; int energy, round; rand_state(&c, hand, &energy, &round);
          for (int arch = 0; arch < 2; arch++) {
              c.brain = NULL; c.arch = arch; c.w_h = 1.0; c.w_n = 0.0; c.sigma = 0.0;
              int h = dwb_choose(&c, hand, energy, round);
              DwPolicyCtx c2 = c; int r = dw_policy_choose(arch == 0 ? DW_POL_RIPPER : DW_POL_WALL, &c2, hand, energy, round);
              tot++; same += (h == r);
          }
      }
      CHECK(same == tot, "prior-only hybrid == heuristic archetype on %ld/%ld states", same, tot); }

    /* 5. seeded determinism of the noisy hybrid */
    { DwPolicyCtx a = mkctx(), b = mkctx(); int8_t hand[4] = {0, 3, 5, 8}; a.brain = b.brain = &def; a.arch = b.arch = 2; a.w_h = b.w_h = 1; a.w_n = b.w_n = 1; a.sigma = b.sigma = 0.5;
      int ok = 1; for (int i = 0; i < 200; i++) ok &= dwb_choose(&a, hand, 6, 4) == dwb_choose(&b, hand, 6, 4);
      CHECK(ok, "same seed -> same decisions"); }

    /* 6. loader rejection: every truncation, trailing byte, header corruption, NaN, and 20k random bit flips */
    { const uint8_t *blob = dw_brain_default_blob; size_t len = dw_brain_default_blob_len; DwBrain t;
      for (size_t n = 0; n < len; n++) { CHECK(!dwb_load_mem(&t, blob, n), "prefix %zu must be rejected", n); }
      uint8_t *m = (uint8_t *)malloc(len + 4); memcpy(m, blob, len); memset(m + len, 0, 4);
      CHECK(!dwb_load_mem(&t, m, len + 4), "trailing bytes rejected");
      memcpy(m, blob, len); m[0] = 'X'; CHECK(!dwb_load_mem(&t, m, len), "bad magic");
      memcpy(m, blob, len); put32(m + 4, 2); CHECK(!dwb_load_mem(&t, m, len), "bad version");
      memcpy(m, blob, len); put32(m + 8, 2); CHECK(!dwb_load_mem(&t, m, len), "bad layer count");
      memcpy(m, blob, len); m[12 + 8] = 0; CHECK(!dwb_load_mem(&t, m, len), "wrong activation (layer0 must be tanh)");
      memcpy(m, blob, len); put32(m + 12, 57); CHECK(!dwb_load_mem(&t, m, len), "wrong obs width");
      memcpy(m, blob, len); put32(m + 12 + 4, 100000); CHECK(!dwb_load_mem(&t, m, len), "absurd hidden width (no huge alloc)");
      memcpy(m, blob, len); putf(m + 39 + 8, NAN); CHECK(!dwb_load_mem(&t, m, len), "NaN weight rejected");
      memcpy(m, blob, len); putf(m + 39 + 12, INFINITY); CHECK(!dwb_load_mem(&t, m, len), "Inf weight rejected");
      for (int it = 0; it < 20000; it++) {
          memcpy(m, blob, len); int flips = 1 + (int)(rnd() % 3);
          for (int f = 0; f < flips; f++) m[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8));
          DwBrain z;
          if (dwb_load_mem(&z, m, len)) {
              double o[DW_OBS_DIM] = {0}, out[5]; CHECK(dwb_logits(&z, o, out), "accepted mutant still runs");
              /* an accepted mutant may have odd (even overflowing) weights, but the legality mask must still hold */
              DwPolicyCtx c = mkctx(); int8_t hand[4]; int energy, round; rand_state(&c, hand, &energy, &round);
              c.brain = &z; c.arch = 1; c.w_h = 1.0; c.w_n = 1.0; int sl = dwb_choose(&c, hand, energy, round);
              CHECK(sl == -1 || (sl >= 0 && sl <= 3 && hand[sl] >= 0 && is_legal_play(hand[sl], energy)), "mutant net produced illegal play");
              dwb_free(&z);
          }
      }
      free(m); }
    dwb_free(&def);
    printf("test_brain: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
