/* dw_bot -- standing DEADWEIGHT bot (S503-05). Connects like any human client (kind=bot), queues, plays, re-queues
 * forever (or --matches N). HONESTY NOTE: these are hand-written HEURISTIC archetypes (ripper / wall / mirror),
 * not learned policies; the training league (S503-08) produces the learned ones. */
#include "client.h"
#include <signal.h>
#include <stdlib.h>
#include "card_rules.h"
#include "iduna.h"
#include "version.h"

static volatile int stop_flag = 0;
static void on_signal(int s) { (void)s; stop_flag = 1; }

static void sleep_interruptible(int ms) { for (int t = 0; t < ms && !stop_flag; t += 100) dw_sleep_ms(100); }

int main(int argc, char **argv) {
    const char *host = "127.0.0.1", *arch = "ripper", *name = NULL, *iduna_url = NULL, *secret_file = NULL, *agent_name = "DEADWEIGHT-BOTS"; int port = 7700, think = 0; long matches = 0; uint32_t seed = 1; int quiet = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("dw_bot %s (rules: %d cards, hull %d)\n", DW_VERSION, num_cards(), start_hull()); return 0; }
        else if (!strcmp(argv[i], "--archetype") && i + 1 < argc) arch = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--matches") && i + 1 < argc) matches = atol(argv[++i]);
        else if (!strcmp(argv[i], "--think-ms") && i + 1 < argc) think = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--iduna-url") && i + 1 < argc) iduna_url = argv[++i];
        else if (!strcmp(argv[i], "--agent-secret-file") && i + 1 < argc) secret_file = argv[++i];
        else if (!strcmp(argv[i], "--agent-name") && i + 1 < argc) agent_name = argv[++i];
        else if (!strcmp(argv[i], "--verbose")) quiet = 0;
        else {
            fprintf(stderr, "usage: dw_bot --archetype ripper|wall|mirror [--name N] [--host H] [--port P] [--matches N] [--think-ms N] [--seed S] [--iduna-url URL --agent-secret-file F [--agent-name N]] [--verbose]\n"
                            "heuristic archetype bots (hand-written rules, NOT learned policies)\n");
            return 2;
        }
    }
    int pol = dw_policy_from_name(arch);
    if (pol != DW_POL_RIPPER && pol != DW_POL_WALL && pol != DW_POL_MIRROR && pol != DW_POL_RANDOM) { fprintf(stderr, "dw_bot: unknown archetype %s\n", arch); return 2; }
    char defname[DW_NAME_LEN + 1]; snprintf(defname, sizeof defname, "bot-%s", arch); if (!name) name = defname;
    if (dw_net_init() != 0) return 1;
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);

    DwRunOpts o; memset(&o, 0, sizeof o);
    o.name = name; o.kind = DW_KIND_BOT; o.policy = pol; o.seed = seed; o.target_matches = matches; o.think_ms = think;
    o.verbose = !quiet; o.stop = &stop_flag;
    DwIduna idu; int use_idu = 0;
    if (iduna_url) {
        int rc = dwi_configure(&idu, iduna_url, agent_name, secret_file);
        if (rc != 0) { fprintf(stderr, "dw_bot: cannot configure IDUNA (%s, rc %d)\n", iduna_url, rc); return 1; }
        use_idu = 1;
    }
    int backoff = 500;
    while (!stop_flag) {
        DwClient c;
        o.token = NULL;
        if (use_idu) {   /* fresh agent JWT per connection (they live ~1h; only HELLO-time validity matters) */
            if (dwi_agent_login(&idu) == 0) o.token = idu.token;
            else fprintf(stderr, "dw_bot %s: IDUNA agent login failed, retry in %d ms\n", name, backoff);
        }
        if ((!use_idu || o.token) && dwc_connect(&c, host, port) == 0) {
            long before = o.done;
            int rc = dwc_run(&c, &o);
            dwc_close(&c);
            if (rc == 0) break;
            if (o.done > before) backoff = 500;
            if (rc == -3) break;
        }
        if (!quiet) fprintf(stderr, "dw_bot %s: disconnected, retry in %d ms\n", name, backoff);
        sleep_interruptible(backoff);
        if (backoff < 8000) backoff *= 2;
    }
    fprintf(stderr, "dw_bot %s: matches=%ld wins=%ld losses=%ld draws=%ld\n", name, o.done, o.wins, o.losses, o.draws);
    return 0;
}
