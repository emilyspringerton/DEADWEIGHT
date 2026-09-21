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

/* Steam zero-friction login (S507). `ticket_hex` is the hex-encoded ISteamUser::GetAuthSessionTicket
 * blob -- obtaining it is real Steamworks-SDK client work this function does NOT do (the SDK is
 * proprietary/Valve-partner-gated and not vendored in this repo; see docs/STEAM_AUTH.md). This
 * function only speaks the already-real IDUNA HTTP contract (docs/IDUNA_CONTRACT.md), same shape
 * as dwi_guest_login. 0 ok (fills player_id/tok/out_tickets); is_new set 1 iff this call caused a
 * brand-new shadow account + starter-ticket grant. -1 network/config error, -2 IDUNA rejected the
 * ticket (bad/expired/banned -- caller should surface an error, not silently fall back to guest). */
int dwi_steam_login(DwIduna *d, const char *ticket_hex, char *player_id, size_t pn, char *tok, size_t tn, int *out_tickets, int *out_is_new);
/* Read a player's current Draft ticket balance (public route, no auth needed). 0 ok. */
int dwi_ticket_balance(DwIduna *d, const char *player_id, int *out_tickets);
/* Consume one Draft ticket server-side (DEADWEIGHT-SERVER agent only -- never called by a client
 * directly, matching dwi_report's own "server reports, client never does" separation). 0 ok
 * (fills out_tickets with the remaining balance), -2 insufficient tickets (HTTP 402). */
int dwi_ticket_consume(DwIduna *d, const char *player_id, int *out_tickets);

/* Redeem an Itch.io claim code (S508 -- "Redeem Code" main-menu box) with the player's OWN token
 * (guest or steam, not an agent). 0 ok (fills out_tickets_granted/out_founder/out_balance), -1
 * network error, -2 IDUNA rejected the code (invalid/already-used/wrong game/bad auth). */
int dwi_redeem(DwIduna *d, const char *player_token, const char *code, int *out_tickets_granted, int *out_founder, int *out_balance);
#endif
