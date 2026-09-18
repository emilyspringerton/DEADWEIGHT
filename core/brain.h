/* Hybrid bot brain (S503-14b): hand-written MLP + heuristic prior, decision math in PARENA (core/bot_brain.c,
 * generated from PARENA/stdlib/deadweight/bot_brain.prn). This file is the hand-written glue: weight-blob loader,
 * 58-float observation builder (layout owned by training/dw_env.py -- keep byte-for-byte in sync), exploration
 * noise, and the call into the PARENA functions.
 *
 * Blob = BRAWLPIT's "BPMW" v1 format (scripts/export_policy_weights.py): magic, u32 version=1, u32 num_layers,
 * per layer {u32 in, u32 out, u8 act(0 linear,1 tanh)}, then per layer W (out*in float32 row-major) + b (out float32),
 * little-endian. Uncompressed on purpose: ~12 KB, LZ4 (the monorepo default) would buy nothing here.
 * VS0 accepts exactly 3 layers: 58 -> H1 tanh -> H2 tanh -> 5 linear, hidden widths 1..128. */
#ifndef DW_BRAIN_H
#define DW_BRAIN_H
#include "parena_runtime.h"   /* FIRST: it sets the POSIX feature macros, which must precede any system header */
#include <stddef.h>
#include <stdint.h>
#include "policy.h"

#define DW_OBS_DIM 58
#define DW_N_ACTIONS 5
#define DW_BRAIN_MAX_HIDDEN 128

typedef struct DwBrain {
    Arena arena;              /* owns the weight Vecs: a DwBrain must not be copied/moved after a successful load */
    Vec w[3], b[3];
    int n[4];                 /* layer widths: 58, H1, H2, 5 */
    int loaded;
} DwBrain;

/* 1 = ok, 0 = rejected (bad magic/version/shape/size/non-finite). *b untouched on failure. */
int dwb_load_mem(DwBrain *b, const uint8_t *data, size_t len);
int dwb_load_file(DwBrain *b, const char *path);
int dwb_load_default(DwBrain *b);        /* the checked-in, embedded distilled weights */
void dwb_free(DwBrain *b);

void dwb_build_obs(double obs[DW_OBS_DIM], const DwPolicyCtx *c, const int8_t hand[4], int energy_you, int round);
/* raw MLP logits for an observation (5 = slots 0..3 then pass). 1 ok, 0 if not loaded. */
int dwb_logits(const DwBrain *b, const double obs[DW_OBS_DIM], double out[DW_N_ACTIONS]);
/* the brain's decision: hand slot 0..3 or -1 (pass); never illegal. */
int dwb_choose(DwPolicyCtx *c, const int8_t hand[4], int energy, int round);   /* matches DwDecideFn */
#endif
