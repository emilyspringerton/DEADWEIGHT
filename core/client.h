/* Blocking protocol client used by dw_client and dw_bot: connect/send/recv-with-timeout plus a match-playing driver. */
#ifndef DW_CLIENT_H
#define DW_CLIENT_H
#include "net.h"
#include "protocol.h"
#include "policy.h"

typedef struct { dw_sock fd; uint8_t in[2048]; size_t n; } DwClient;

int dwc_connect(DwClient *c, const char *host, int port);      /* 0 ok */
int dwc_send(DwClient *c, const DwMsg *m);                     /* 0 ok */
int dwc_recv(DwClient *c, DwMsg *m, int timeout_ms);           /* 1 msg, 0 timeout, -1 closed/error/malformed */
void dwc_close(DwClient *c);

typedef struct {
    const char *name; int kind;            /* DW_KIND_* */
    const char *token;                     /* optional IDUNA token; sent as AUTH after HELLO (needed when the server requires auth) */
    int policy; uint32_t seed;
    int mode;                              /* DW_MODE_CARD (random, default) or DW_MODE_DRAFT: draft a deck, then requeue same-deck on win/draw, redraft on loss */
    /* DW_POL_HYBRID only: brain config copied into the policy context (survives per-match resets) */
    DwDecideFn decide; const struct DwBrain *brain; int arch; double w_h, w_n, sigma;
    long target_matches;                   /* stop after this many completed matches (<=0: forever) */
    int think_ms;                          /* artificial delay before each play */
    int verbose;
    int idle_timeout_ms;                   /* give up (-1) after this long with no server message; 0 = wait forever */
    volatile int *stop;                    /* optional: set nonzero (e.g. from a signal handler) to return -3 */
    /* out */
    long done, wins, losses, draws;
} DwRunOpts;

/* Connect-less driver: HELLO, QUEUE, play matches until target reached. Returns 0 = target reached,
 * -1 = connection lost/error/idle timeout (caller may reconnect), -2 = server rejected us (ERROR frame),
 * -3 = stop requested. */
int dwc_run(DwClient *c, DwRunOpts *o);
#endif
