/* dw_replay_dump: reads ONE raw dw_server --match-log matches.ndjson record on stdin (the exact
 * line format `write_match_log` in apps/server/main.c writes), replays it through the real,
 * authoritative match core (core/match.c -- the same deterministic replay tests/replay_check.c
 * already proves is faithful from (seed, each round's two locked slots)), and prints a full
 * round-by-round trace as JSON on stdout: hull/energy before each round, both hands, which card
 * each seat actually played (declared + effective, e.g. Dark Pool substitution), damage/heal, and
 * hull/energy after. This is real engine replay, not a reconstruction guess -- WOTAN's replay
 * viewer (S547) steps through this trace card-by-card instead of showing only the final box score.
 *
 * Usage: dw_replay_dump < one_match_line.json
 * Exit: 0 on success (even if replay hit "bad" data -- caller checks the JSON "ok" field), 2 on
 * unusable input (couldn't even parse a match_id/seed/plays).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "match.h"
#include "protocol.h"

#define MAXLINE (1 << 20) /* 1 MiB -- matches.ndjson lines are well under this even for draft mode */

static char *slurp_stdin(void) {
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            if (cap > MAXLINE) cap = MAXLINE;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        size_t n = fread(buf + len, 1, cap - len - 1, stdin);
        len += n;
        if (n == 0 || len + 1 >= cap) break;
    }
    buf[len] = 0;
    return buf;
}

static char *extract_name(const char *p, char *out, size_t outsz) {
    /* p points just after the opening quote of a JSON string value */
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outsz) {
        if (*p == '\\' && p[1]) p++; /* skip escape char crudely -- names are server-controlled bot/player names, no embedded quotes in practice */
        out[i++] = *p++;
    }
    out[i] = 0;
    return out;
}

int main(void) {
    char *line = slurp_stdin();
    if (!line || !*line) { fprintf(stderr, "dw_replay_dump: empty input\n"); return 2; }

    unsigned match_id = 0, seed = 0;
    int kind0 = 0, kind1 = 0, r0 = 0, r1 = 0, reason = 0, rounds_reported = 0;
    char name0[128] = "", name1[128] = "";

    char *p;
    if ((p = strstr(line, "\"match_id\":"))) match_id = (unsigned)strtoul(p + 11, NULL, 10);
    if ((p = strstr(line, "\"seed\":"))) seed = (unsigned)strtoul(p + 7, NULL, 10);
    if ((p = strstr(line, "\"names\":["))) {
        p += 9;
        if (*p == '"') extract_name(p + 1, name0, sizeof name0);
        char *comma = strchr(p, ',');
        if (comma && comma[1] == '"') extract_name(comma + 2, name1, sizeof name1);
    }
    if ((p = strstr(line, "\"kinds\":["))) sscanf(p + 9, "%d,%d", &kind0, &kind1);
    if ((p = strstr(line, "\"result\":["))) sscanf(p + 10, "%d,%d", &r0, &r1);
    if ((p = strstr(line, "\"reason\":"))) reason = atoi(p + 9);
    if ((p = strstr(line, "\"rounds\":"))) rounds_reported = atoi(p + 9);

    char *pp = strstr(line, "\"plays\":[");
    if (!pp) { fprintf(stderr, "dw_replay_dump: no \"plays\" field\n"); free(line); return 2; }
    pp += 9;

    DwMatch m;
    int is_draft = 0;
    unsigned deck_id0 = 0, deck_id1 = 0;
    int8_t dk[2][DW_DRAFT_DECK];
    char *modep = strstr(line, "\"mode\":\"draft\"");
    char *dp = strstr(line, "\"decks\":[[");
    if (modep && dp) {
        is_draft = 1;
        char *dip = strstr(line, "\"deck_ids\":[");
        if (dip) sscanf(dip + 12, "%u,%u", &deck_id0, &deck_id1);
        dp += 9;
        int okd = 1;
        for (int sd = 0; sd < 2 && okd; sd++) {
            if (*dp != '[') { okd = 0; break; }
            dp++;
            for (int i = 0; i < DW_DRAFT_DECK; i++) {
                int v, n = 0;
                if (sscanf(dp, "%d%n", &v, &n) != 1) { okd = 0; break; }
                dk[sd][i] = (int8_t)v; dp += n;
                if (*dp == ',') dp++;
            }
            if (okd && *dp == ']') { dp++; if (*dp == ',') dp++; } else okd = 0;
        }
        if (!okd) { fprintf(stderr, "dw_replay_dump: bad decks array\n"); free(line); return 2; }
        dw_match_init_decks(&m, seed, dk[0], DW_DRAFT_DECK, dk[1], DW_DRAFT_DECK);
    } else {
        dw_match_init(&m, seed);
    }

    printf("{\"match_id\":%u,\"seed\":%u,\"names\":[\"%s\",\"%s\"],\"kinds\":[%d,%d],"
           "\"result\":[%d,%d],\"reason\":%d,\"rounds_reported\":%d,\"mode\":\"%s\"",
           match_id, seed, name0, name1, kind0, kind1, r0, r1, reason, rounds_reported,
           is_draft ? "draft" : "card");
    if (is_draft) {
        printf(",\"deck_ids\":[%u,%u],\"decks\":[[", deck_id0, deck_id1);
        for (int i = 0; i < DW_DRAFT_DECK; i++) printf("%s%d", i ? "," : "", dk[0][i]);
        printf("],[");
        for (int i = 0; i < DW_DRAFT_DECK; i++) printf("%s%d", i ? "," : "", dk[1][i]);
        printf("]]");
    }

    printf(",\"replay_ok\":true,\"rounds_data\":[");
    int bad = 0, first = 1;
    while (*pp == '[' && !m.done) {
        int a, b, n = 0;
        if (sscanf(pp, "[%d,%d]%n", &a, &b, &n) != 2) { bad = 1; break; }
        pp += n;
        if (*pp == ',') pp++;

        dw_match_begin_round(&m);
        int hull0_before = m.hull[0], hull1_before = m.hull[1];
        int energy0_before = m.energy[0], energy1_before = m.energy[1];
        int armor0_before = m.armor[0], armor1_before = m.armor[1];
        int8_t hand0[DW_HAND], hand1[DW_HAND];
        memcpy(hand0, m.hand[0], DW_HAND);
        memcpy(hand1, m.hand[1], DW_HAND);

        if (dw_match_lock(&m, 0, a) || dw_match_lock(&m, 1, b)) { bad = 1; break; }
        DwOutcome o;
        dw_match_resolve(&m, &o);

        if (!first) printf(",");
        first = 0;
        printf("{\"round\":%d,"
               "\"hull_before\":[%d,%d],\"energy_before\":[%d,%d],\"armor_before\":[%d,%d],"
               "\"hand\":[[%d,%d,%d,%d],[%d,%d,%d,%d]],\"slot\":[%d,%d],"
               "\"card\":[%d,%d],\"eff\":[%d,%d],"
               "\"dmg\":[%d,%d],\"heal\":[%d,%d],\"roll\":[%d,%d],\"flags\":[%d,%d],"
               "\"hull_after\":[%d,%d],\"energy_after\":[%d,%d]}",
               m.round,
               hull0_before, hull1_before, energy0_before, energy1_before, armor0_before, armor1_before,
               hand0[0], hand0[1], hand0[2], hand0[3], hand1[0], hand1[1], hand1[2], hand1[3],
               a, b,
               o.card[0], o.card[1], o.eff[0], o.eff[1],
               o.dmg_to[0], o.dmg_to[1], o.heal[0], o.heal[1], o.roll[0], o.roll[1], o.flags[0], o.flags[1],
               m.hull[0], m.hull[1], m.energy[0], m.energy[1]);
    }
    printf("],\"replay_bad\":%s,\"final_hull\":[%d,%d],\"final_done\":%s}\n",
           bad ? "true" : "false", m.hull[0], m.hull[1], m.done ? "true" : "false");

    free(line);
    return 0;
}
