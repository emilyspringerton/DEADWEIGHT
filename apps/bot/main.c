/* dw_bot -- standing DEADWEIGHT bot (S503-05). Connects like any human client (kind=bot), queues, plays, re-queues
 * forever (or --matches N). --mode constructed: the bot plays decks it did not draft -- see DeckSrc below. HONESTY NOTE: --brain heuristic = the hand-written archetype rules (ripper / wall / mirror).
 * --brain hybrid (default) = the same archetype prior blended with a small hand-written MLP (decision math in PARENA,
 * core/bot_brain.c) whose checked-in default weights are DISTILLED from an expected-value teacher, NOT RL-trained;
 * the league (S503-08) produces RL weights that drop in via --weights. */
#include "brain.h"   /* first: parena_runtime.h must precede every system header (POSIX feature macros) */
#include "client.h"
#include <signal.h>
#include <stdlib.h>
#include "card_rules.h"
#include "iduna.h"
#include "version.h"

static volatile int stop_flag = 0;
static void on_signal(int s) { (void)s; stop_flag = 1; }

static void sleep_interruptible(int ms) { for (int t = 0; t < ms && !stop_flag; t += 100) dw_sleep_ms(100); }

/* ---- constructed mode: where the bot's decks come from ----
 * The bot's play brain is unchanged (it plays whatever hand it is dealt); what makes constructed bots different from each other is
 * the DECK. A bot is given a strength tier (--tier N of --tiers T, 1 = weakest) and keeps a pool of decks from that slice of
 * IDUNA's draft-deck tracker (GET .../bot-decks: the draft decks ranked by how long they kept winning), refreshed every
 * --deck-refresh-min minutes; each match it submits a random one from the pool. If IDUNA cannot be reached it keeps the last pool,
 * else the decks in --deck-file, else a random legal deck. The server pairs a human with the waiting bot whose ladder rating is
 * closest, so the tiers sort themselves by rating and a low-rated player meets the bots with the weaker decks. */
typedef struct {
    DwIduna *idu; int tier, tiers, n, refresh_ms;
    DwDeckPool pool; uint64_t fetched_ms; int fetched_ok;
    const char *file; int file_loaded;
    uint32_t rng; int verbose;
} DeckSrc;

static uint32_t dxs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }

static void load_deck_file(DeckSrc *s) {
    s->file_loaded = 1;
    FILE *f = fopen(s->file, "r");
    if (!f) { fprintf(stderr, "dw_bot: cannot read --deck-file %s\n", s->file); return; }
    char line[512];
    while (s->pool.n < DW_POOL_MAX && fgets(line, sizeof line, f)) {
        int8_t d[DW_DECK_SIZE];
        if (line[0] != '#' && dw_deck_parse(line, d)) memcpy(s->pool.deck[s->pool.n++], d, sizeof d);
    }
    fclose(f);
}

static int deck_next(void *ctx, int8_t out[DW_DECK_SIZE]) {
    DeckSrc *s = (DeckSrc *)ctx;
    if (s->idu && s->tier > 0) {
        uint64_t now = dw_now_ms();
        if (!s->fetched_ms || now - s->fetched_ms >= (uint64_t)(s->fetched_ok ? s->refresh_ms : 30000)) {   /* retry a failed fetch every 30 s */
            DwDeckPool p;
            if (dwi_bot_decks(s->idu, s->tier, s->tiers, s->n, &p) == 0 && p.n > 0) {
                s->pool = p; s->fetched_ok = 1;
                if (s->verbose) printf("dw_bot: tier %d/%d: %d decks from IDUNA\n", s->tier, s->tiers, p.n);
            } else { s->fetched_ok = 0; fprintf(stderr, "dw_bot: tier %d/%d: no decks from IDUNA (keeping %d)\n", s->tier, s->tiers, s->pool.n); }
            s->fetched_ms = now;
        }
    }
    if (s->pool.n == 0 && s->file && !s->file_loaded) load_deck_file(s);
    if (s->pool.n == 0) return -1;   /* the client falls back to a random legal deck */
    memcpy(out, s->pool.deck[dxs(&s->rng) % (uint32_t)s->pool.n], DW_DECK_SIZE);
    return 0;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1", *arch = "ripper", *name = NULL, *iduna_url = NULL, *secret_file = NULL, *agent_name = "DEADWEIGHT-BOTS"; int port = 7700, think = 0; const char *brain_mode = "hybrid", *weights = NULL; double w_h = 0.3, w_n = 1.0, sigma = 0.1;   /* tuned by tests/brain_winrates.sh: archetype flavour as a bias on top of the net */ long matches = 0; uint32_t seed = 1; int quiet = 1, mode = DW_MODE_CARD;
    const char *deck_file = NULL; int tier = 0, tiers = 5, deck_n = 24, refresh_min = 30;
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
        else if (!strcmp(argv[i], "--brain") && i + 1 < argc) brain_mode = argv[++i];
        else if (!strcmp(argv[i], "--weights") && i + 1 < argc) weights = argv[++i];
        else if (!strcmp(argv[i], "--wh") && i + 1 < argc) w_h = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wn") && i + 1 < argc) w_n = atof(argv[++i]);
        else if (!strcmp(argv[i], "--sigma") && i + 1 < argc) sigma = atof(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) { const char *mm = argv[++i]; if (!strcmp(mm, "draft")) mode = DW_MODE_DRAFT; else if (!strcmp(mm, "random")) mode = DW_MODE_CARD; else if (!strcmp(mm, "constructed")) mode = DW_MODE_CONSTRUCTED; else { fprintf(stderr, "dw_bot: --mode must be random, draft or constructed\n"); return 2; } }
        else if (!strcmp(argv[i], "--tier") && i + 1 < argc) tier = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tiers") && i + 1 < argc) tiers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--deck-n") && i + 1 < argc) deck_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--deck-refresh-min") && i + 1 < argc) refresh_min = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--deck-file") && i + 1 < argc) deck_file = argv[++i];
        else if (!strcmp(argv[i], "--verbose")) quiet = 0;
        else {
            fprintf(stderr, "usage: dw_bot --archetype ripper|wall|mirror [--name N] [--host H] [--port P] [--matches N] [--think-ms N] [--seed S] [--iduna-url URL --agent-secret-file F [--agent-name N]] [--mode random|draft|constructed [--tier N [--tiers T] [--deck-n N] [--deck-refresh-min M]] [--deck-file F]] [--brain hybrid|heuristic] [--weights FILE] [--wh W] [--wn W] [--sigma S] [--verbose]\n"
                            "archetype bots: hybrid = heuristic prior + hand-written MLP (default weights distilled, NOT RL-trained); heuristic = rules only\n");
            return 2;
        }
    }
    int pol = dw_policy_from_name(arch);
    if (pol != DW_POL_RIPPER && pol != DW_POL_WALL && pol != DW_POL_MIRROR && pol != DW_POL_RANDOM) { fprintf(stderr, "dw_bot: unknown archetype %s\n", arch); return 2; }
    int hybrid = !strcmp(brain_mode, "hybrid");
    if (!hybrid && strcmp(brain_mode, "heuristic")) { fprintf(stderr, "dw_bot: --brain must be hybrid or heuristic\n"); return 2; }
    static DwBrain brain; int arch_id = pol == DW_POL_RIPPER ? 0 : pol == DW_POL_WALL ? 1 : pol == DW_POL_MIRROR ? 2 : -1;
    if (hybrid && arch_id < 0) { fprintf(stderr, "dw_bot: hybrid brain needs --archetype ripper|wall|mirror\n"); return 2; }
    if (hybrid) {
        int ok = weights ? dwb_load_file(&brain, weights) : dwb_load_default(&brain);
        if (!ok) { fprintf(stderr, "dw_bot: cannot load brain weights (%s): bad or missing BPMW blob\n", weights ? weights : "embedded default"); return 1; }
        pol = DW_POL_HYBRID;
    }
    char defname[DW_NAME_LEN + 1]; snprintf(defname, sizeof defname, "bot-%s", arch); if (!name) name = defname;
    if (dw_net_init() != 0) return 1;
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);

    DwRunOpts o; memset(&o, 0, sizeof o);
    o.name = name; o.kind = DW_KIND_BOT; o.policy = pol; o.mode = mode;
    if (hybrid) { o.decide = dwb_choose; o.brain = &brain; o.arch = arch_id; o.w_h = w_h; o.w_n = w_n; o.sigma = sigma; } o.seed = seed; o.target_matches = matches; o.think_ms = think;
    o.verbose = !quiet; o.stop = &stop_flag;
    DwIduna idu; int use_idu = 0;
    if (iduna_url) {
        int rc = dwi_configure(&idu, iduna_url, agent_name, secret_file);
        if (rc != 0) { fprintf(stderr, "dw_bot: cannot configure IDUNA (%s, rc %d)\n", iduna_url, rc); return 1; }
        use_idu = 1;
    }
    if (mode != DW_MODE_CONSTRUCTED && (tier || deck_file)) { fprintf(stderr, "dw_bot: --tier/--deck-file only apply to --mode constructed\n"); return 2; }
    if (tier < 0 || tiers < 1 || tiers > 10 || tier > tiers) { fprintf(stderr, "dw_bot: --tier must be 1..--tiers (tiers 1..10)\n"); return 2; }
    if (tier && !use_idu) { fprintf(stderr, "dw_bot: --tier needs --iduna-url (the decks come from IDUNA's tracker)\n"); return 2; }
    static DeckSrc src;
    if (mode == DW_MODE_CONSTRUCTED) {
        src.idu = use_idu ? &idu : NULL; src.tier = tier; src.tiers = tiers; src.n = deck_n; src.refresh_ms = (refresh_min < 1 ? 1 : refresh_min) * 60000;
        src.file = deck_file; src.verbose = !quiet; src.rng = seed * 2246822519u ^ 0x9E3779B1u; if (!src.rng) src.rng = 1;
        o.next_deck = deck_next; o.deck_ctx = &src;
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
