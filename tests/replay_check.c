/* Replays every hull/rounds-ended match in a dw_server --match-log ndjson file through the match core and checks
 * the recorded result: the log is a faithful, replayable record. Usage: replay_check matches.ndjson [min_lines] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "match.h"
#include "protocol.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: replay_check FILE [min_replayed]\n"); return 2; }
    FILE *f = fopen(argv[1], "r"); if (!f) { perror("open"); return 2; }
    long minr = argc > 2 ? atol(argv[2]) : 1, replayed = 0, skipped = 0, bad = 0;
    static char line[4096];
    while (fgets(line, sizeof line, f)) {
        unsigned seed; int r0, r1, reason;
        char *p = strstr(line, "\"seed\":"); if (!p) { bad++; continue; } seed = (unsigned)strtoul(p + 7, NULL, 10);
        p = strstr(line, "\"result\":["); if (!p) { bad++; continue; } if (sscanf(p, "\"result\":[%d,%d],\"reason\":%d", &r0, &r1, &reason) != 3) { bad++; continue; }
        if (reason != DW_END_HULL && reason != DW_END_ROUNDS && reason != DW_END_BANKRUPT) { skipped++; continue; }
        p = strstr(line, "\"plays\":["); if (!p) { bad++; continue; } p += 9;
        DwMatch m;
        char *dp = strstr(line, "\"decks\":[[");
        if (dp) {   /* draft match: each seat played its recorded 23-card deck */
            int8_t dk[2][DW_DRAFT_DECK]; int okd = 1; dp += 9;
            for (int sd = 0; sd < 2 && okd; sd++) {
                if (*dp != '[') { okd = 0; break; }
                dp++;
                for (int i = 0; i < DW_DRAFT_DECK; i++) { int v, n = 0; if (sscanf(dp, "%d%n", &v, &n) != 1) { okd = 0; break; } dk[sd][i] = (int8_t)v; dp += n; if (*dp == ',') dp++; }
                if (okd && *dp == ']') { dp++; if (*dp == ',') dp++; } else okd = 0;
            }
            if (!okd) { bad++; continue; }
            dw_match_init_decks(&m, seed, dk[0], DW_DRAFT_DECK, dk[1], DW_DRAFT_DECK);
        } else dw_match_init(&m, seed);
        while (*p == '[') {
            int a, b, n = 0;
            if (sscanf(p, "[%d,%d]%n", &a, &b, &n) != 2) { bad++; break; }
            p += n; if (*p == ',') p++;
            dw_match_begin_round(&m);
            if (dw_match_lock(&m, 0, a) || dw_match_lock(&m, 1, b)) { bad++; break; }
            dw_match_resolve(&m, NULL);
        }
        if (!m.done || m.result[0] != r0 || m.result[1] != r1 || m.reason != reason) { bad++; fprintf(stderr, "replay mismatch: seed %u\n", seed); }
        else replayed++;
    }
    fclose(f);
    printf("replay_check: %ld replayed OK, %ld skipped (forfeit/server), %ld bad\n", replayed, skipped, bad);
    return (bad || replayed < minr) ? 1 : 0;
}
