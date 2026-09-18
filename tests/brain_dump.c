/* Dump tool for the Python parity test (training/test_dw_brain.py): reads states from stdin, one per line:
 *   hull_you hull_opp energy_you energy_opp round h0 h1 h2 h3 opp_hand_size k0 k1 k2   (13 ints)
 * and prints "obs <58 doubles>" then "logits <5 doubles>" (%.17g) using the real C glue + PARENA forward pass. */
#include "brain.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: brain_dump WEIGHTS.bin < states\n"); return 2; }
    DwBrain b; if (!dwb_load_file(&b, argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
    int v[13];
    for (;;) {
        int n = 0; while (n < 13 && scanf("%d", &v[n]) == 1) n++;
        if (n == 0) break;
        if (n != 13) { fprintf(stderr, "short state\n"); return 1; }
        DwPolicyCtx c; dw_policy_reset(&c, 1);
        c.hull_you = v[0]; c.hull_opp = v[1]; c.energy_opp = v[3]; c.opp_hand_size = v[9]; c.opp_kinds[0] = v[10]; c.opp_kinds[1] = v[11]; c.opp_kinds[2] = v[12];
        int8_t hand[4] = {(int8_t)v[5], (int8_t)v[6], (int8_t)v[7], (int8_t)v[8]};
        double obs[DW_OBS_DIM], lg[DW_N_ACTIONS];
        dwb_build_obs(obs, &c, hand, v[2], v[4]);
        if (!dwb_logits(&b, obs, lg)) { fprintf(stderr, "forward failed\n"); return 1; }
        printf("obs");
        for (int i = 0; i < DW_OBS_DIM; i++) printf(" %.17g", obs[i]);
        printf("\nlogits");
        for (int i = 0; i < DW_N_ACTIONS; i++) printf(" %.17g", lg[i]);
        printf("\n");
    }
    dwb_free(&b);
    return 0;
}
