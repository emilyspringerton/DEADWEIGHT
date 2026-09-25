/* IDUNA client for the DEADWEIGHT game contract (docs/IDUNA_CONTRACT.md): agent login, token verify, match-result
 * reporting, guest register/login. Blocking HTTP with a short timeout -- callers that must not block (dw_server)
 * run these on a worker thread. */
#ifndef DW_IDUNA_H
#define DW_IDUNA_H
#include <stddef.h>

typedef struct {
    char host[128]; int port; int use_tls; int configured;
    char agent_name[64]; char agent_secret[256];
    char token[1536];                 /* cached agent JWT */
} DwIduna;

typedef struct { char player_id[48]; char display_name[48]; int kind; /* 0 human, 1 bot */ } DwIdentity;

/* S537: friends/profiles/friendly-challenge duels (IDUNA internal/http/handlers/game_social.go) --
 * the same identity/routes WOTAN's friends.html/profile.html and the web client's social.ts use. */
typedef struct { char player_id[48]; char display_name[48]; int rating; int wins, losses, draws, matches; int friend_count; } DwProfile;
typedef struct { int id; char requester_id[48]; char recipient_id[48]; char status[16]; } DwFriendRequest;
typedef struct { char player_id[48]; char display_name[48]; int rating; } DwFriendSummary;
/* match_token: 32 hex chars + NUL, only ever populated by IDUNA while status=="accepted" and the
 * token's 15-min TTL hasn't expired (game_social.go's duelsList) -- empty means "no live token",
 * whether because it's still pending, was declined, or the window already lapsed. */
typedef struct { int id; char challenger_id[48]; char challenged_id[48]; char status[16]; char match_token[33]; } DwDuel;

typedef struct {
    unsigned match_id, seed; char seat_pid[2][48];
    int winner;                       /* 0 / 1 / 2 = draw */
    int rounds, reason;               /* reason: DW_END_* */
    int mode;                         /* DW_MODE_CARD / DW_MODE_DRAFT (protocol.h) -- S508c: real,
                                        * found-live bug fix, this field didn't exist and dwi_report
                                        * hardcoded "mode":0 in every match-result POST, meaning
                                        * IDUNA's game_matches.mode (and the new Uncapped Draft
                                        * Run tracking, which reads this same field) has been wrong
                                        * for every historical draft match ever reported. */
} DwMatchReport;

/* url = http://host:port. secret_file: raw secret, or an env-style file containing IDUNA_SECRET_<NAME>=... 0 ok. */
int dwi_configure(DwIduna *d, const char *url, const char *agent_name, const char *secret_file);
int dwi_agent_login(DwIduna *d);                                   /* 0 ok; fills d->token */
/* 0 verified, -1 IDUNA unreachable/5xx, -2 rejected (401/403/other 4xx). token is passed as a Bearer header. */
int dwi_verify(DwIduna *d, const char *token, const char *bot_name, DwIdentity *out);
/* POST match-result as the server agent (logs in / re-logs in on 401). 0 ok (incl. duplicate), -1 failure. */
int dwi_report(DwIduna *d, const DwMatchReport *r);
/* Guest accounts. Return 0 ok; token written to tok (>= 1536 bytes). display_name may be "" or
 * NULL (S512 zero-friction auth: the release client no longer collects one at all) -- IDUNA
 * auto-assigns a lore-friendly one ("Runner-A7B2") in that case, always readable back via
 * out_name (0 ok even if out_name is NULL; NULL out_tickets is also fine, both optional).
 * -1 network/config error (out_status set 0), -2 IDUNA rejected the request (out_status set to
 * the real HTTP status -- S517 found-live bug: a 429 "too many new accounts from this IP today"
 * was being shown to the player as "IDUNA offline", the network-failure message, which sent
 * debugging in the wrong direction; out_status lets the caller tell these apart. out_status may
 * be NULL if the caller doesn't need to distinguish). out_is_guest (S522, optional) is filled
 * from the response's own "account_state" ("guest" vs "base", computed server-side from
 * player_credentials) -- this is what the Claim Account UI affordance should gate on, not just
 * "no email typed in yet" (a returning claimed player logging back in must never see the button). */
int dwi_guest_register(DwIduna *d, const char *display_name, char *player_id, size_t pn, char *secret, size_t sn, char *tok, size_t tn,
                        char *out_name, size_t on, int *out_tickets, int *out_status, int *out_is_guest);
int dwi_guest_login(DwIduna *d, const char *player_id, const char *secret, char *tok, size_t tn, char *out_name, size_t on, int *out_tickets, int *out_status, int *out_is_guest);

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

/* Uncapped Draft Run (S508c/S510) -- takes the PLAYER'S OWN token (guest or steam), not an agent:
 * S510 found this was never actually wired into apps/server/main.c's poll loop (would have
 * needed a whole new async job type + wire messages to stay off the poll() loop), and every
 * action here only ever touches the calling player's OWN balance/run row (win/loss increments
 * stay locked to the agent-authenticated match-result report in dwi_report), so it's the same
 * trust level as dwi_redeem/dwi_guest_upgrade, which already call IDUNA directly with the
 * player's token -- callable straight from the GUI client, no dw_server round-trip needed.
 * Idempotent: if the player already has an active run, resumes it for free (out_ticket_spent=0);
 * otherwise spends 1 ticket and starts a fresh one. 0 ok (fills out_wins/out_losses/
 * out_ticket_spent), -2 insufficient tickets (HTTP 402) when no run is active and the balance is
 * 0, -1 network/config error. */
int dwi_draft_run_start(DwIduna *d, const char *player_token, int *out_wins, int *out_losses, int *out_ticket_spent);
/* Read the player's current Draft Run state (S510) -- for the boot-time "Draft Hub" resume check
 * ("if draft_active == true, route to SCREEN_DRAFT_HUB"). 0 ok (fills out_active/out_wins/
 * out_losses/out_deck; *out_deck_n is how many of deck_cap slots were filled), -1 on failure. */
int dwi_draft_run_state(DwIduna *d, const char *player_token, int *out_active, int *out_wins, int *out_losses,
                         int *out_deck, int deck_cap, int *out_deck_n);
/* Persist the deck the client's local draft picker just finished (S510) -- so a boot-time resume
 * and the leaderboard have a real server-side record, not just what dw_server holds in memory for
 * one connection. 0 ok, -2 no active run to attach it to, -1 network/config error. */
int dwi_draft_run_save_deck(DwIduna *d, const char *player_token, const int *deck, int deck_n);
/* "Abort & Extract" (S510): voluntary cash-out before the 3rd loss, at whatever win count the run
 * currently holds. 0 ok (fills out_wins/out_losses/out_tickets_granted/out_balance), -2 no
 * active run, -1 network/config error. */
int dwi_draft_run_abort(DwIduna *d, const char *player_token, int *out_wins, int *out_losses,
                         int *out_tickets_granted, int *out_balance);

/* Guest -> email upgrade ("Link Email, Save Progress"): player_token is the player's OWN existing
 * token (any provider). Keeps the same player_id -- tickets/stats/founder-flag/draft-run all
 * carry over automatically. 0 ok (fills tok, >= 1536 bytes), -2 IDUNA rejected (bad email/
 * password/already-linked -- out_status distinguishes: 409 means this exact email already
 * belongs to some OTHER player_id, the real trigger for a login-fallback rather than a bad
 * request; may be NULL if the caller doesn't need to tell those apart). */
int dwi_guest_upgrade(DwIduna *d, const char *player_token, const char *email, const char *password, char *tok, size_t tn, int *out_status);
/* Returning email/password login -- same game-scoped token shape every other login path here
 * issues. 0 ok, -2 invalid credentials. */
int dwi_email_login(DwIduna *d, const char *email, const char *password, char *tok, size_t tn, int *out_tickets);

/* Redeem an Itch.io claim code (S508 -- "Redeem Code" main-menu box) with the player's OWN token
 * (guest or steam, not an agent). 0 ok (fills out_tickets_granted/out_founder/out_balance), -1
 * network error, -2 IDUNA rejected the code (invalid/already-used/wrong game/bad auth). */
int dwi_redeem(DwIduna *d, const char *player_token, const char *code, int *out_tickets_granted, int *out_founder, int *out_balance);

/* Friends, public profiles, friendly-challenge duels (S537). Public GET, no auth needed. 0 ok, -1
 * not found / network error. */
int dwi_profile(DwIduna *d, const char *player_id, DwProfile *out);
/* Everything below takes the CALLER'S OWN token (guest, steam, or email-linked -- any provider,
 * same trust level as dwi_redeem/dwi_guest_upgrade above; friends/duels never need email-linking,
 * unlike WOTAN's own friends.html which only requires that because it's a separate site with no
 * other way to obtain a session). 0 ok, -2 IDUNA rejected (self-friend, not-yet-friends for a
 * duel, already exists, etc. -- out_status carries the real HTTP status for a caller that wants
 * to distinguish, may be NULL), -1 network/config error. */
int dwi_friend_request_send(DwIduna *d, const char *player_token, const char *to_player_id, int *out_status);
int dwi_friend_requests_list(DwIduna *d, const char *player_token,
                              DwFriendRequest *out_incoming, int max_incoming, int *out_incoming_n,
                              DwFriendRequest *out_outgoing, int max_outgoing, int *out_outgoing_n);
int dwi_friend_request_respond(DwIduna *d, const char *player_token, int request_id, int accept);
int dwi_friends_list(DwIduna *d, const char *player_token, DwFriendSummary *out, int max_n, int *out_n);
int dwi_friend_remove(DwIduna *d, const char *player_token, const char *friend_player_id);
int dwi_duel_create(DwIduna *d, const char *player_token, const char *to_player_id, int *out_id, int *out_status);
int dwi_duels_list(DwIduna *d, const char *player_token, DwDuel *out, int max_n, int *out_n);
int dwi_duel_respond(DwIduna *d, const char *player_token, int duel_id, int accept);
#endif
