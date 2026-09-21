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
    if (dw_parse_url(url, d->host, sizeof d->host, &d->port) != 0) return -1;
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
    if (dw_http("POST", d->host, d->port, "/api/v1/auth/agent", NULL, body, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    if (!dw_json_str(resp, "access_token", d->token, sizeof d->token)) return -1;
    return 0;
}

int dwi_verify(DwIduna *d, const char *token, const char *bot_name, DwIdentity *out) {
    char body[128] = "", resp[2048], kind[16]; int st = 0;
    if (bot_name && *bot_name) snprintf(body, sizeof body, "{\"name\":\"%s\"}", bot_name);
    if (dw_http("POST", d->host, d->port, GAME "/verify", token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
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
        if (dw_http("POST", d->host, d->port, GAME "/match-result", d->token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
        if (st == 401) { d->token[0] = 0; continue; }
        return st == 200 ? 0 : -1;
    }
    return -1;
}

int dwi_guest_register(DwIduna *d, const char *display_name, char *pid, size_t pn, char *secret, size_t sn, char *tok, size_t tn) {
    char body[128], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"display_name\":\"%s\"}", display_name);
    if (dw_http("POST", d->host, d->port, GAME "/guest-register", NULL, body, resp, sizeof resp, &st, TMO) != 0 || st != 201) return -1;
    if (!dw_json_str(resp, "player_id", pid, pn) || !dw_json_str(resp, "guest_secret", secret, sn) || !dw_json_str(resp, "token", tok, tn)) return -1;
    return 0;
}

int dwi_guest_login(DwIduna *d, const char *pid, const char *secret, char *tok, size_t tn) {
    char body[256], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"player_id\":\"%s\",\"guest_secret\":\"%s\"}", pid, secret);
    if (dw_http("POST", d->host, d->port, GAME "/guest-login", NULL, body, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    return dw_json_str(resp, "token", tok, tn) ? 0 : -1;
}

int dwi_steam_login(DwIduna *d, const char *ticket_hex, char *pid, size_t pn, char *tok, size_t tn, int *out_tickets, int *out_is_new) {
    char body[2048], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"ticket\":\"%s\"}", ticket_hex);
    if (dw_http("POST", d->host, d->port, GAME "/steam-login", NULL, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st != 200) return -2; /* 404 not configured, 401 rejected ticket, 403 banned, 501 server not configured */
    if (!dw_json_str(resp, "player_id", pid, pn) || !dw_json_str(resp, "token", tok, tn)) return -1;
    if (out_tickets) { int v = 0; dw_json_int(resp, "tickets", &v); *out_tickets = v; }
    if (out_is_new) { int v = 0; dw_json_int(resp, "is_new", &v); *out_is_new = v; }
    return 0;
}

int dwi_ticket_balance(DwIduna *d, const char *pid, int *out_tickets) {
    char path[128], resp[512]; int st = 0;
    snprintf(path, sizeof path, GAME "/players/%s/tickets", pid);
    if (dw_http("GET", d->host, d->port, path, NULL, NULL, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    return dw_json_int(resp, "tickets", out_tickets) ? 0 : -1;
}

int dwi_redeem(DwIduna *d, const char *player_token, const char *code, int *out_tickets_granted, int *out_founder, int *out_balance) {
    char body[64], resp[512]; int st = 0;
    snprintf(body, sizeof body, "{\"code\":\"%s\"}", code);
    if (dw_http("POST", d->host, d->port, GAME "/redeem", player_token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st != 200) return -2;
    if (out_tickets_granted) { int v = 0; dw_json_int(resp, "tickets_granted", &v); *out_tickets_granted = v; }
    if (out_founder) { int v = 0; dw_json_int(resp, "founder", &v); *out_founder = v; }
    if (out_balance) { int v = 0; dw_json_int(resp, "tickets", &v); *out_balance = v; }
    return 0;
}

int dwi_draft_run_start(DwIduna *d, const char *pid, int *out_wins, int *out_losses, int *out_ticket_spent) {
    char body[128], resp[256]; int st = 0;
    snprintf(body, sizeof body, "{\"player_id\":\"%s\"}", pid);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!d->token[0] && dwi_agent_login(d) != 0) return -1;
        if (dw_http("POST", d->host, d->port, GAME "/draft-run/start", d->token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
        if (st == 401) { d->token[0] = 0; continue; }
        if (st == 402) return -2;
        if (st != 200) return -1;
        if (out_wins) { int v = 0; dw_json_int(resp, "wins", &v); *out_wins = v; }
        if (out_losses) { int v = 0; dw_json_int(resp, "losses", &v); *out_losses = v; }
        if (out_ticket_spent) { int v = 0; dw_json_int(resp, "ticket_spent", &v); *out_ticket_spent = v; }
        return 0;
    }
    return -1;
}

int dwi_guest_upgrade(DwIduna *d, const char *player_token, const char *email, const char *password, char *tok, size_t tn) {
    char body[512], resp[2048]; int st = 0;
    snprintf(body, sizeof body, "{\"email\":\"%s\",\"password\":\"%s\"}", email, password);
    if (dw_http("POST", d->host, d->port, GAME "/guest-upgrade", player_token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st != 200) return -2;
    return dw_json_str(resp, "token", tok, tn) ? 0 : -1;
}

int dwi_email_login(DwIduna *d, const char *email, const char *password, char *tok, size_t tn, int *out_tickets) {
    char body[512], resp[2048]; int st = 0;
    snprintf(body, sizeof body, "{\"email\":\"%s\",\"password\":\"%s\"}", email, password);
    if (dw_http("POST", d->host, d->port, GAME "/email-login", NULL, body, resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st != 200) return -2;
    if (out_tickets) { int v = 0; dw_json_int(resp, "tickets", &v); *out_tickets = v; }
    return dw_json_str(resp, "token", tok, tn) ? 0 : -1;
}

int dwi_ticket_consume(DwIduna *d, const char *pid, int *out_tickets) {
    char body[128], resp[512]; int st = 0;
    snprintf(body, sizeof body, "{\"player_id\":\"%s\"}", pid);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!d->token[0] && dwi_agent_login(d) != 0) return -1;
        if (dw_http("POST", d->host, d->port, GAME "/tickets/consume", d->token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
        if (st == 401) { d->token[0] = 0; continue; }
        if (st == 402) return -2;
        if (st != 200) return -1;
        return dw_json_int(resp, "tickets", out_tickets) ? 0 : -1;
    }
    return -1;
}
