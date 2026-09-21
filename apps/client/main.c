/* dw_client -- DEADWEIGHT headless CLI client (Linux + Windows). Plays interactively from stdin, or automatically
 * with --auto random|first-legal. Exit code 0 = the requested number of matches completed. */
#include "client.h"
#include <stdlib.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#include "card_rules.h"
#include "iduna.h"
#include "version.h"

int main(int argc, char **argv) {
    const char *host = "127.0.0.1", *name = "player", *iduna_url = NULL, *guest_file = NULL, *raw_token = NULL; int port = 7700, kind = DW_KIND_HUMAN, policy = DW_POL_INTERACTIVE;
    long matches = 1; uint32_t seed = 1; int quiet = 0, think = 0, mode = DW_MODE_CARD;
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
        else if (!strcmp(argv[i], "--iduna-url") && i + 1 < argc) iduna_url = argv[++i];
        else if (!strcmp(argv[i], "--guest-file") && i + 1 < argc) guest_file = argv[++i];
        else if (!strcmp(argv[i], "--token") && i + 1 < argc) raw_token = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) { i++; if (!strcmp(argv[i], "draft")) mode = DW_MODE_DRAFT; else if (!strcmp(argv[i], "random")) mode = DW_MODE_CARD; else { fprintf(stderr, "--mode must be random or draft\n"); return 2; } }
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else { fprintf(stderr, "usage: dw_client [--host H] [--port P] [--name N] [--kind human|bot] [--auto random|first-legal|ripper|wall|mirror] [--mode random|draft] [--matches N] [--seed S] [--think-ms N] [--iduna-url URL --guest-file F | --token JWT] [--quiet] [--version]\n"); return 2; }
    }
    if (dw_net_init() != 0) return 1;
    static char tok[1536];
    if (iduna_url) {   /* guest account: register on first use (file holds player_id + guest_secret; lose it = account gone), else login */
        DwIduna idu; char pid[64] = "", sec[128] = "";
        if (!guest_file || dwi_configure(&idu, iduna_url, "", NULL) != 0) { fprintf(stderr, "dw_client: --iduna-url needs a valid URL and --guest-file\n"); return 2; }
        FILE *gf = fopen(guest_file, "r");
        if (gf) {
            char line[200];
            while (fgets(line, sizeof line, gf)) {
                size_t n = strlen(line); while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
                if (!strncmp(line, "player_id=", 10)) snprintf(pid, sizeof pid, "%.63s", line + 10);
                else if (!strncmp(line, "guest_secret=", 13)) snprintf(sec, sizeof sec, "%.127s", line + 13);
            }
            fclose(gf);
        }
        if (pid[0] && sec[0]) {
            if (dwi_guest_login(&idu, pid, sec, tok, sizeof tok, NULL, 0, NULL) != 0) { fprintf(stderr, "dw_client: guest-login failed (account unrecoverable without its file)\n"); return 1; }
        } else {
            if (dwi_guest_register(&idu, name, pid, sizeof pid, sec, sizeof sec, tok, sizeof tok, NULL, 0, NULL) != 0) { fprintf(stderr, "dw_client: guest-register failed\n"); return 1; }
            gf = fopen(guest_file, "w");
            if (!gf) { fprintf(stderr, "dw_client: cannot write %s\n", guest_file); return 1; }
            fprintf(gf, "player_id=%s\nguest_secret=%s\n", pid, sec); fclose(gf);
#ifndef _WIN32
            chmod(guest_file, 0600);
#endif
            printf("dw_client: registered guest %s (saved to %s)\n", pid, guest_file);
        }
    }
    DwClient c;
    if (dwc_connect(&c, host, port)) { fprintf(stderr, "dw_client: cannot connect to %s:%d\n", host, port); return 1; }
    DwRunOpts o; memset(&o, 0, sizeof o);
    o.name = name; o.kind = kind; o.policy = policy; o.seed = seed; o.target_matches = matches; o.think_ms = think; o.verbose = !quiet; o.idle_timeout_ms = 60000; o.token = tok[0] ? tok : raw_token; o.mode = mode;
    int rc = dwc_run(&c, &o);
    dwc_close(&c);
    printf("dw_client: matches=%ld wins=%ld losses=%ld draws=%ld\n", o.done, o.wins, o.losses, o.draws);
    return rc == 0 ? 0 : 1;
}
