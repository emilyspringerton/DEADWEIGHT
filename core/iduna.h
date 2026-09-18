/* IDUNA client for the DEADWEIGHT game contract (docs/IDUNA_CONTRACT.md): agent login, token verify, match-result
 * reporting, guest register/login. Blocking HTTP with a short timeout -- callers that must not block (dw_server)
 * run these on a worker thread. */
#ifndef DW_IDUNA_H
#define DW_IDUNA_H
#include <stddef.h>

typedef struct {
    char host[128]; int port; int configured;
    char agent_name[64]; char agent_secret[256];
    char token[1536];                 /* cached agent JWT */
} DwIduna;

typedef struct { char player_id[48]; char display_name[48]; int kind; /* 0 human, 1 bot */ } DwIdentity;

typedef struct {
    unsigned match_id, seed; char seat_pid[2][48];
    int winner;                       /* 0 / 1 / 2 = draw */
    int rounds, reason;               /* reason: DW_END_* */
} DwMatchReport;

/* url = http://host:port. secret_file: raw secret, or an env-style file containing IDUNA_SECRET_<NAME>=... 0 ok. */
int dwi_configure(DwIduna *d, const char *url, const char *agent_name, const char *secret_file);
int dwi_agent_login(DwIduna *d);                                   /* 0 ok; fills d->token */
/* 0 verified, -1 IDUNA unreachable/5xx, -2 rejected (401/403/other 4xx). token is passed as a Bearer header. */
int dwi_verify(DwIduna *d, const char *token, const char *bot_name, DwIdentity *out);
/* POST match-result as the server agent (logs in / re-logs in on 401). 0 ok (incl. duplicate), -1 failure. */
int dwi_report(DwIduna *d, const DwMatchReport *r);
/* Guest accounts. Return 0 ok; token written to tok (>= 1536 bytes). */
int dwi_guest_register(DwIduna *d, const char *display_name, char *player_id, size_t pn, char *secret, size_t sn, char *tok, size_t tn);
int dwi_guest_login(DwIduna *d, const char *player_id, const char *secret, char *tok, size_t tn);
#endif
