/* Blocking protocol client used by dw_client and dw_bot: connect/send/recv-with-timeout plus a match-playing driver. */
#ifndef DW_CLIENT_H
#define DW_CLIENT_H
#include "net.h"
#include "protocol.h"
#include "policy.h"

typedef struct { dw_sock fd; uint8_t in[1024]; size_t n; } DwClient;

int dwc_connect(DwClient *c, const char *host, int port);      /* 0 ok */
int dwc_send(DwClient *c, const DwMsg *m);                     /* 0 ok */
int dwc_recv(DwClient *c, DwMsg *m, int timeout_ms);           /* 1 msg, 0 timeout, -1 closed/error/malformed */
void dwc_close(DwClient *c);

typedef struct {
    const char *name; int kind;            /* DW_KIND_* */
    int policy; uint32_t seed;
    long target_matches;                   /* stop after this many completed matches (<=0: forever) */
    int think_ms;                          /* artificial delay before each play */
    int verbose;
    /* out */
    long done, wins, losses, draws;
} DwRunOpts;

/* Connect-less driver: HELLO, QUEUE, play matches until target reached. Returns 0 = target reached,
 * -1 = connection lost/error (caller may reconnect), -2 = server rejected us (ERROR frame). */
int dwc_run(DwClient *c, DwRunOpts *o);
#endif
