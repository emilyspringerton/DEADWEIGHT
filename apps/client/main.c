/* dw_client -- DEADWEIGHT headless CLI client (Linux + Windows). Plays interactively from stdin, or automatically
 * with --auto random|first-legal. Exit code 0 = the requested number of matches completed. */
#include "client.h"
#include <stdlib.h>
#include "card_rules.h"
#include "version.h"

int main(int argc, char **argv) {
    const char *host = "127.0.0.1", *name = "player"; int port = 7700, kind = DW_KIND_HUMAN, policy = DW_POL_INTERACTIVE;
    long matches = 1; uint32_t seed = 1; int quiet = 0, think = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("dw_client %s (rules: %d cards, hull %d)\n", DW_VERSION, num_cards(), start_hull()); return 0; }
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--kind") && i + 1 < argc) { i++; kind = !strcmp(argv[i], "bot") ? DW_KIND_BOT : DW_KIND_HUMAN; }
        else if (!strcmp(argv[i], "--auto") && i + 1 < argc) { policy = dw_policy_from_name(argv[++i]); if (policy < 0) { fprintf(stderr, "unknown --auto policy\n"); return 2; } }
        else if (!strcmp(argv[i], "--matches") && i + 1 < argc) matches = atol(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--think-ms") && i + 1 < argc) think = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else { fprintf(stderr, "usage: dw_client [--host H] [--port P] [--name N] [--kind human|bot] [--auto random|first-legal|ripper|wall|mirror] [--matches N] [--seed S] [--think-ms N] [--quiet] [--version]\n"); return 2; }
    }
    if (dw_net_init() != 0) return 1;
    DwClient c;
    if (dwc_connect(&c, host, port)) { fprintf(stderr, "dw_client: cannot connect to %s:%d\n", host, port); return 1; }
    DwRunOpts o; memset(&o, 0, sizeof o);
    o.name = name; o.kind = kind; o.policy = policy; o.seed = seed; o.target_matches = matches; o.think_ms = think; o.verbose = !quiet; o.idle_timeout_ms = 60000;
    int rc = dwc_run(&c, &o);
    dwc_close(&c);
    printf("dw_client: matches=%ld wins=%ld losses=%ld draws=%ld\n", o.done, o.wins, o.losses, o.draws);
    return rc == 0 ? 0 : 1;
}
