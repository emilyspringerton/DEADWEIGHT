/* Heuristic card-mode policies shared by dw_client --auto and dw_bot. These are HEURISTICS (hand-written
 * archetype rules), not learned policies; the training league (S503-08) produces the learned ones. */
#ifndef DW_POLICY_H
#define DW_POLICY_H
#include <stdint.h>
enum { DW_POL_RANDOM = 0, DW_POL_FIRST_LEGAL, DW_POL_RIPPER, DW_POL_WALL, DW_POL_MIRROR, DW_POL_INTERACTIVE };
typedef struct { uint32_t rng; int last_opp_card; } DwPolicyCtx;   /* last_opp_card: -1 = pass / none yet */
int dw_policy_from_name(const char *name);                          /* -1 if unknown */
void dw_policy_reset(DwPolicyCtx *c, uint32_t seed);
/* Returns hand slot 0..3 or -1 (pass). Always legal for the given energy. */
int dw_policy_choose(int policy, DwPolicyCtx *c, const int8_t hand[4], int energy, int round);
#endif
