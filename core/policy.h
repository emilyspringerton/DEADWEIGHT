/* Heuristic card-mode policies shared by dw_client --auto and dw_bot. These are HEURISTICS (hand-written
 * archetype rules), not learned policies; the training league (S503-08) produces the learned ones. */
#ifndef DW_POLICY_H
#define DW_POLICY_H
#include <stdint.h>
enum { DW_POL_RANDOM = 0, DW_POL_FIRST_LEGAL, DW_POL_RIPPER, DW_POL_WALL, DW_POL_MIRROR, DW_POL_INTERACTIVE, DW_POL_HYBRID };
struct DwBrain;
struct DwPolicyCtx;
typedef int (*DwDecideFn)(struct DwPolicyCtx *c, const int8_t hand[4], int energy, int round);
typedef struct DwPolicyCtx {
    uint32_t rng; int last_opp_card;                      /* last_opp_card: -1 = pass / none yet */
    /* observation state kept current by the client driver (only DW_POL_HYBRID reads it) */
    int hull_you, hull_opp, energy_opp, opp_hand_size;
    int vault_you, lock_mask;                             /* your credits and hand slots locked this round (legality) */
    int opp_kinds[3];                                     /* opponent's last 3 played kinds, oldest first; 3 = none/pass */
    /* hybrid brain configuration (set once by the caller, survives dw_policy_reset) */
    DwDecideFn decide;                                    /* DW_POL_HYBRID entry point (dwb_choose); NULL elsewhere so non-bot binaries need no brain code */
    const struct DwBrain *brain; int arch;                /* arch: 0 ripper, 1 wall, 2 mirror */
    double w_h, w_n, sigma;                               /* prior weight, net weight, exploration noise stddev */
} DwPolicyCtx;
int dw_policy_from_name(const char *name);                          /* -1 if unknown */
void dw_policy_reset(DwPolicyCtx *c, uint32_t seed);
/* Returns hand slot 0..3 or -1 (pass). Always legal for the given energy. */
uint32_t dw_policy_rand(DwPolicyCtx *c);                          /* xorshift step (shared with the brain's noise) */
int dw_policy_choose(int policy, DwPolicyCtx *c, const int8_t hand[4], int energy, int round);
#endif
