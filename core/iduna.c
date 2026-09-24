#include "net.h"
#include <ctype.h>
#include <stdlib.h>
#include "iduna.h"
#include "http.h"
#include "protocol.h"

#define TMO 3000
#define GAME "/api/v1/games/deadweight"

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    size_t i = 0; while (isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

int dwi_configure(DwIduna *d, const char *url, const char *agent_name, const char *secret_file) {
    memset(d, 0, sizeof *d);
    if (dw_parse_url(url, d->host, sizeof d->host, &d->port, &d->use_tls) != 0) return -1;
    snprintf(d->agent_name, sizeof d->agent_name, "%s", agent_name ? agent_name : "");
    if (secret_file && *secret_file) {
        FILE *f = fopen(secret_file, "r"); if (!f) return -2;
        char key[96] = "IDUNA_SECRET_", line[512], whole[256] = ""; size_t k = strlen(key);
        for (const char *p = d->agent_name; *p && k + 1 < sizeof key; p++) key[k++] = *p == '-' ? '_' : (char)toupper((unsigned char)*p);
        key[k] = 0;
        int found = 0, lines = 0;
        while (fgets(line, sizeof line, f)) {
            lines++;
            trim(line);
            if (!strncmp(line, "export ", 7)) memmove(line, line + 7, strlen(line + 7) + 1);   /* agent-secrets.env uses `export KEY=...` */
            if (!strncmp(line, key, k) && line[k] == '=') { snprintf(d->agent_secret, sizeof d->agent_secret, "%.255s", line + k + 1); found = 1; }
            else if (lines == 1) snprintf(whole, sizeof whole, "%.255s", line);
        }
        fclose(f);
        if (!found && lines == 1 && !strchr(whole, '=')) snprintf(d->agent_secret, sizeof d->agent_secret, "%s", whole), found = 1;
        if (!found) return -3;
        /* strip optional surrounding quotes */
        size_t n = strlen(d->agent_secret);
        if (n >= 2 && (d->agent_secret[0] == '"' || d->agent_secret[0] == '\'') && d->agent_secret[n - 1] == d->agent_secret[0]) { memmove(d->agent_secret, d->agent_secret + 1, n - 2); d->agent_secret[n - 2] = 0; }
    }
    d->configured = 1;
    return 0;
}

int dwi_agent_login(DwIduna *d) {
    char body[512], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}", d->agent_name, d->agent_secret);
    if (dw_http("POST", d->host, d->port, d->use_tls, "/api/v1/auth/agent", NULL, body, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    if (!dw_json_str(resp, "access_token", d->token, sizeof d->token)) return -1;
    return 0;
}

int dwi_verify(DwIduna *d, const char *token, const char *bot_name, DwIdentity *out) {
    char body[128] = "", resp[2048], kind[16]; int st = 0;
    if (bot_name && *bot_name) snprintf(body, sizeof body, "{\"name\":\"%s\"}", bot_name);
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/verify", token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st >= 500) return -1;
    if (st != 200) return -2;
    memset(out, 0, sizeof *out);
    if (!dw_json_str(resp, "player_id", out->player_id, sizeof out->player_id)) return -2;
    dw_json_str(resp, "display_name", out->display_name, sizeof out->display_name);
    out->kind = (dw_json_str(resp, "kind", kind, sizeof kind) && !strcmp(kind, "bot")) ? 1 : 0;
    return 0;
}

int dwi_report(DwIduna *d, const DwMatchReport *r) {
    static const char *const reasons[] = { "hull", "rounds", "forfeit", "server" };
    char body[512], resp[2048]; int st = 0;
    snprintf(body, sizeof body, "{\"match_id\":%u,\"seed\":%u,\"seat0_player_id\":\"%s\",\"seat1_player_id\":\"%s\",\"winner\":%d,\"rounds\":%d,\"reason\":\"%s\",\"mode\":%d}",
             r->match_id, r->seed, r->seat_pid[0], r->seat_pid[1], r->winner, r->rounds, reasons[r->reason & 3], r->mode);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!d->token[0] && dwi_agent_login(d) != 0) return -1;
        if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/match-result", d->token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
        if (st == 401) { d->token[0] = 0; continue; }
        return st == 200 ? 0 : -1;
    }
    return -1;
}

/* S522: "account_state" ("guest" vs "base", computed server-side from whether player_credentials
 * has a row -- see IDUNA's accountState()) is what the Claim Account UI gates on: the button only
 * shows for a real guest, never for an already-claimed account restored from a saved session. */
static int accountStateIsGuest(const char *resp) {
    char state[16] = "";
    dw_json_str(resp, "account_state", state, sizeof state);
    return strcmp(state, "guest") == 0;
}

int dwi_guest_register(DwIduna *d, const char *display_name, char *pid, size_t pn, char *secret, size_t sn, char *tok, size_t tn,
                        char *out_name, size_t on, int *out_tickets, int *out_status, int *out_is_guest) {
    char body[128], resp[4096]; int st = 0;
    /* S512 zero-friction auth: an empty display_name is the real, expected path now -- IDUNA
     * auto-assigns a lore-friendly name ("Runner-A7B2") when it sees one, returned in "display_name"
     * below same as an explicitly-chosen name would be. */
    snprintf(body, sizeof body, "{\"display_name\":\"%s\"}", display_name ? display_name : "");
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/guest-register", NULL, body, resp, sizeof resp, &st, TMO) != 0) { if (out_status) *out_status = 0; return -1; }
    if (out_status) *out_status = st;
    if (st != 201) return -2;
    if (!dw_json_str(resp, "player_id", pid, pn) || !dw_json_str(resp, "guest_secret", secret, sn) || !dw_json_str(resp, "token", tok, tn)) return -1;
    if (out_name) dw_json_str(resp, "display_name", out_name, on);
    if (out_tickets) { int v = 0; dw_json_int(resp, "tickets", &v); *out_tickets = v; }
    if (out_is_guest) *out_is_guest = accountStateIsGuest(resp);
    return 0;
}

int dwi_guest_login(DwIduna *d, const char *pid, const char *secret, char *tok, size_t tn, char *out_name, size_t on, int *out_tickets, int *out_status, int *out_is_guest) {
    char body[256], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"player_id\":\"%s\",\"guest_secret\":\"%s\"}", pid, secret);
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/guest-login", NULL, body, resp, sizeof resp, &st, TMO) != 0) { if (out_status) *out_status = 0; return -1; }
    if (out_status) *out_status = st;
    if (st != 200) return -2;
    if (out_is_guest) *out_is_guest = accountStateIsGuest(resp);
    if (!dw_json_str(resp, "token", tok, tn)) return -1;
    if (out_name) dw_json_str(resp, "display_name", out_name, on);
    if (out_tickets) { int v = 0; dw_json_int(resp, "tickets", &v); *out_tickets = v; }
    return 0;
}

int dwi_steam_login(DwIduna *d, const char *ticket_hex, char *pid, size_t pn, char *tok, size_t tn, int *out_tickets, int *out_is_new) {
    char body[2048], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"ticket\":\"%s\"}", ticket_hex);
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/steam-login", NULL, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st != 200) return -2; /* 404 not configured, 401 rejected ticket, 403 banned, 501 server not configured */
    if (!dw_json_str(resp, "player_id", pid, pn) || !dw_json_str(resp, "token", tok, tn)) return -1;
    if (out_tickets) { int v = 0; dw_json_int(resp, "tickets", &v); *out_tickets = v; }
    if (out_is_new) { int v = 0; dw_json_int(resp, "is_new", &v); *out_is_new = v; }
    return 0;
}

int dwi_ticket_balance(DwIduna *d, const char *pid, int *out_tickets) {
    char path[128], resp[512]; int st = 0;
    snprintf(path, sizeof path, GAME "/players/%s/tickets", pid);
    if (dw_http("GET", d->host, d->port, d->use_tls, path, NULL, NULL, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    return dw_json_int(resp, "tickets", out_tickets) ? 0 : -1;
}

int dwi_redeem(DwIduna *d, const char *player_token, const char *code, int *out_tickets_granted, int *out_founder, int *out_balance) {
    char body[64], resp[512]; int st = 0;
    snprintf(body, sizeof body, "{\"code\":\"%s\"}", code);
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/redeem", player_token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st != 200) return -2;
    if (out_tickets_granted) { int v = 0; dw_json_int(resp, "tickets_granted", &v); *out_tickets_granted = v; }
    if (out_founder) { int v = 0; dw_json_int(resp, "founder", &v); *out_founder = v; }
    if (out_balance) { int v = 0; dw_json_int(resp, "tickets", &v); *out_balance = v; }
    return 0;
}

int dwi_draft_run_start(DwIduna *d, const char *player_token, int *out_wins, int *out_losses, int *out_ticket_spent) {
    char resp[256]; int st = 0;
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/draft-run/start", player_token, "{}", resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st == 402) return -2;
    if (st != 200) return -1;
    if (out_wins) { int v = 0; dw_json_int(resp, "wins", &v); *out_wins = v; }
    if (out_losses) { int v = 0; dw_json_int(resp, "losses", &v); *out_losses = v; }
    if (out_ticket_spent) { int v = 0; dw_json_int(resp, "ticket_spent", &v); *out_ticket_spent = v; }
    return 0;
}

int dwi_draft_run_state(DwIduna *d, const char *player_token, int *out_active, int *out_wins, int *out_losses,
                         int *out_deck, int deck_cap, int *out_deck_n) {
    char resp[2048]; int st = 0;
    if (dw_http("GET", d->host, d->port, d->use_tls, GAME "/draft-run", player_token, NULL, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    if (out_active) { int v = 0; dw_json_int(resp, "active", &v); *out_active = v; }
    if (out_wins) { int v = 0; dw_json_int(resp, "wins", &v); *out_wins = v; }
    if (out_losses) { int v = 0; dw_json_int(resp, "losses", &v); *out_losses = v; }
    if (out_deck && out_deck_n) *out_deck_n = dw_json_int_array(resp, "deck", out_deck, deck_cap);
    return 0;
}

int dwi_draft_run_save_deck(DwIduna *d, const char *player_token, const int *deck, int deck_n) {
    char body[512], resp[128]; int st = 0;
    size_t off = (size_t)snprintf(body, sizeof body, "{\"deck\":[");
    for (int i = 0; i < deck_n && off + 8 < sizeof body; i++)
        off += (size_t)snprintf(body + off, sizeof body - off, "%s%d", i ? "," : "", deck[i]);
    snprintf(body + off, sizeof body - off, "]}");
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/draft-run/deck", player_token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st == 400) return -2;
    return st == 200 ? 0 : -1;
}

int dwi_draft_run_abort(DwIduna *d, const char *player_token, int *out_wins, int *out_losses,
                         int *out_tickets_granted, int *out_balance) {
    char resp[256]; int st = 0;
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/draft-run/abort", player_token, "{}", resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st == 400) return -2;
    if (st != 200) return -1;
    if (out_wins) { int v = 0; dw_json_int(resp, "wins", &v); *out_wins = v; }
    if (out_losses) { int v = 0; dw_json_int(resp, "losses", &v); *out_losses = v; }
    if (out_tickets_granted) { int v = 0; dw_json_int(resp, "tickets_granted", &v); *out_tickets_granted = v; }
    if (out_balance) { int v = 0; dw_json_int(resp, "tickets", &v); *out_balance = v; }
    return 0;
}

int dwi_guest_upgrade(DwIduna *d, const char *player_token, const char *email, const char *password, char *tok, size_t tn) {
    char body[512], resp[2048]; int st = 0;
    snprintf(body, sizeof body, "{\"email\":\"%s\",\"password\":\"%s\"}", email, password);
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/guest-upgrade", player_token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st != 200) return -2;
    return dw_json_str(resp, "token", tok, tn) ? 0 : -1;
}

int dwi_email_login(DwIduna *d, const char *email, const char *password, char *tok, size_t tn, int *out_tickets) {
    char body[512], resp[2048]; int st = 0;
    snprintf(body, sizeof body, "{\"email\":\"%s\",\"password\":\"%s\"}", email, password);
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/email-login", NULL, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st != 200) return -2;
    if (out_tickets) { int v = 0; dw_json_int(resp, "tickets", &v); *out_tickets = v; }
    return dw_json_str(resp, "token", tok, tn) ? 0 : -1;
}

/* --- S537: friends, public profiles, friendly-challenge duels ------------------------------ */

static void fillFriendRequest(const char *obj, DwFriendRequest *out) {
    memset(out, 0, sizeof *out);
    dw_json_int(obj, "id", &out->id);
    dw_json_str(obj, "requester_id", out->requester_id, sizeof out->requester_id);
    dw_json_str(obj, "recipient_id", out->recipient_id, sizeof out->recipient_id);
    dw_json_str(obj, "status", out->status, sizeof out->status);
}

static void fillFriendSummary(const char *obj, DwFriendSummary *out) {
    memset(out, 0, sizeof *out);
    dw_json_str(obj, "player_id", out->player_id, sizeof out->player_id);
    dw_json_str(obj, "display_name", out->display_name, sizeof out->display_name);
    dw_json_int(obj, "rating", &out->rating);
}

static void fillDuel(const char *obj, DwDuel *out) {
    memset(out, 0, sizeof *out);
    dw_json_int(obj, "id", &out->id);
    dw_json_str(obj, "challenger_id", out->challenger_id, sizeof out->challenger_id);
    dw_json_str(obj, "challenged_id", out->challenged_id, sizeof out->challenged_id);
    dw_json_str(obj, "status", out->status, sizeof out->status);
}

int dwi_profile(DwIduna *d, const char *player_id, DwProfile *out) {
    char path[192], resp[1024]; int st = 0;
    snprintf(path, sizeof path, GAME "/players/%s/profile", player_id);
    if (dw_http("GET", d->host, d->port, d->use_tls, path, NULL, NULL, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    memset(out, 0, sizeof *out);
    dw_json_str(resp, "player_id", out->player_id, sizeof out->player_id);
    dw_json_str(resp, "display_name", out->display_name, sizeof out->display_name);
    dw_json_int(resp, "rating", &out->rating);
    dw_json_int(resp, "wins", &out->wins);
    dw_json_int(resp, "losses", &out->losses);
    dw_json_int(resp, "draws", &out->draws);
    dw_json_int(resp, "matches", &out->matches);
    dw_json_int(resp, "friend_count", &out->friend_count);
    return 0;
}

int dwi_friend_request_send(DwIduna *d, const char *player_token, const char *to_player_id, int *out_status) {
    char body[128], resp[512]; int st = 0;
    snprintf(body, sizeof body, "{\"to_player_id\":\"%s\"}", to_player_id);
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/friend-requests", player_token, body, resp, sizeof resp, &st, TMO) != 0) { if (out_status) *out_status = 0; return -1; }
    if (out_status) *out_status = st;
    return (st == 200 || st == 201) ? 0 : -2;
}

int dwi_friend_requests_list(DwIduna *d, const char *player_token,
                              DwFriendRequest *out_incoming, int max_incoming, int *out_incoming_n,
                              DwFriendRequest *out_outgoing, int max_outgoing, int *out_outgoing_n) {
    char resp[4096]; int st = 0;
    if (dw_http("GET", d->host, d->port, d->use_tls, GAME "/friend-requests", player_token, NULL, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    if (out_incoming_n) {
        int n = 0; const char *obj;
        while (n < max_incoming && dw_json_array_at(resp, "incoming", n, &obj)) { fillFriendRequest(obj, &out_incoming[n]); n++; }
        *out_incoming_n = n;
    }
    if (out_outgoing_n) {
        int n = 0; const char *obj;
        while (n < max_outgoing && dw_json_array_at(resp, "outgoing", n, &obj)) { fillFriendRequest(obj, &out_outgoing[n]); n++; }
        *out_outgoing_n = n;
    }
    return 0;
}

int dwi_friend_request_respond(DwIduna *d, const char *player_token, int request_id, int accept) {
    char path[128], resp[256]; int st = 0;
    snprintf(path, sizeof path, GAME "/friend-requests/%d/%s", request_id, accept ? "accept" : "decline");
    if (dw_http("POST", d->host, d->port, d->use_tls, path, player_token, "{}", resp, sizeof resp, &st, TMO) != 0) return -1;
    return st == 200 ? 0 : -2;
}

int dwi_friends_list(DwIduna *d, const char *player_token, DwFriendSummary *out, int max_n, int *out_n) {
    char resp[4096]; int st = 0;
    if (dw_http("GET", d->host, d->port, d->use_tls, GAME "/friends", player_token, NULL, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    int n = 0; const char *obj; /* /friends is a bare top-level array -- key=NULL */
    while (n < max_n && dw_json_array_at(resp, NULL, n, &obj)) { fillFriendSummary(obj, &out[n]); n++; }
    if (out_n) *out_n = n;
    return 0;
}

int dwi_friend_remove(DwIduna *d, const char *player_token, const char *friend_player_id) {
    char path[192], resp[256]; int st = 0;
    snprintf(path, sizeof path, GAME "/friends/%s", friend_player_id);
    if (dw_http("DELETE", d->host, d->port, d->use_tls, path, player_token, NULL, resp, sizeof resp, &st, TMO) != 0) return -1;
    return st == 200 ? 0 : -2;
}

int dwi_duel_create(DwIduna *d, const char *player_token, const char *to_player_id, int *out_id, int *out_status) {
    char body[128], resp[256]; int st = 0;
    snprintf(body, sizeof body, "{\"to_player_id\":\"%s\"}", to_player_id);
    if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/duels", player_token, body, resp, sizeof resp, &st, TMO) != 0) { if (out_status) *out_status = 0; return -1; }
    if (out_status) *out_status = st;
    if (st != 201) return -2;
    if (out_id) dw_json_int(resp, "id", out_id);
    return 0;
}

int dwi_duels_list(DwIduna *d, const char *player_token, DwDuel *out, int max_n, int *out_n) {
    char resp[4096]; int st = 0;
    if (dw_http("GET", d->host, d->port, d->use_tls, GAME "/duels", player_token, NULL, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    int n = 0; const char *obj; /* /duels is a bare top-level array -- key=NULL */
    while (n < max_n && dw_json_array_at(resp, NULL, n, &obj)) { fillDuel(obj, &out[n]); n++; }
    if (out_n) *out_n = n;
    return 0;
}

int dwi_duel_respond(DwIduna *d, const char *player_token, int duel_id, int accept) {
    char path[128], resp[256]; int st = 0;
    snprintf(path, sizeof path, GAME "/duels/%d/%s", duel_id, accept ? "accept" : "decline");
    if (dw_http("POST", d->host, d->port, d->use_tls, path, player_token, "{}", resp, sizeof resp, &st, TMO) != 0) return -1;
    return st == 200 ? 0 : -2;
}

int dwi_ticket_consume(DwIduna *d, const char *pid, int *out_tickets) {
    char body[128], resp[512]; int st = 0;
    snprintf(body, sizeof body, "{\"player_id\":\"%s\"}", pid);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!d->token[0] && dwi_agent_login(d) != 0) return -1;
        if (dw_http("POST", d->host, d->port, d->use_tls, GAME "/tickets/consume", d->token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
        if (st == 401) { d->token[0] = 0; continue; }
        if (st == 402) return -2;
        if (st != 200) return -1;
        return dw_json_int(resp, "tickets", out_tickets) ? 0 : -1;
    }
    return -1;
}
