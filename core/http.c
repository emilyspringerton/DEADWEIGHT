#include "net.h"
#include <stdlib.h>
#include "http.h"
#ifdef PARENA_WITH_TLS
/* S508e -- real mbedTLS FFI binding lives in PARENA's own runtime, not reimplemented here. This
 * pulls in ONLY the tls_*_impl primitives (plain scalar/buffer signatures, no Arena/Result
 * boxing needed), same "take only the real FFI primitive, not the rest of the runtime"
 * discipline PARENA_NO_GRAPHICS already establishes for SDL2 in that same header. */
#include "parena_runtime.h"
#endif

int dw_parse_url(const char *url, char *host, size_t hn, int *port, int *use_tls) {
    const char *p = url;
    if (!strncmp(p, "https://", 8)) { p += 8; *use_tls = 1; }
    else if (!strncmp(p, "http://", 7)) { p += 7; *use_tls = 0; }
    else if (strstr(p, "://")) return -1;
    else *use_tls = 0;
    size_t i = 0;
    while (*p && *p != ':' && *p != '/' && i + 1 < hn) host[i++] = *p++;
    host[i] = 0;
    if (i == 0) return -1;
    *port = *use_tls ? 443 : 80;
    if (*p == ':') { *port = atoi(p + 1); if (*port <= 0 || *port > 65535) return -1; }
    return 0;
}

int dw_json_str(const char *json, const char *key, char *out, size_t n) {
    char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' ) p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"') { if (*p == '\\' || i + 1 >= n) return 0; out[i++] = *p++; }
    if (*p != '"') return 0;
    out[i] = 0;
    return 1;
}

int dw_json_int(const char *json, const char *key, int *out) {
    char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (!strncmp(p, "true", 4)) { *out = 1; return 1; }
    if (!strncmp(p, "false", 5)) { *out = 0; return 1; }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = (int)v;
    return 1;
}

/* Parses a flat JSON array of non-negative small ints, e.g. "deck":[1,2,3] -- the only array
 * shape any DEADWEIGHT<->IDUNA response needs (the draft-run deck). Not a general JSON parser:
 * no nesting, no negative numbers, no whitespace tolerance beyond what strtol itself skips. */
int dw_json_int_array(const char *json, const char *key, int *out, int max_n) {
    char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '[') return 0;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max_n) {
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) { p++; continue; }
        out[n++] = (int)v;
        p = end;
        while (*p == ',' || *p == ' ') p++;
    }
    return n;
}

static int wait_readable(dw_sock s, int timeout_ms) {
    struct pollfd p; p.fd = s; p.events = POLLIN; p.revents = 0;
    return dw_poll(&p, 1, timeout_ms);
}

int dw_http(const char *method, const char *host, int port, int use_tls, const char *path, const char *bearer,
            const char *json_body, char *resp, size_t resp_n, int *status, int timeout_ms) {
    enum { REQ_MAX = 4096, RAW_MAX = 16384 };
    char *req = malloc(REQ_MAX), *raw = malloc(RAW_MAX);
    int rc = -1; dw_sock s = DW_BAD_SOCK;
#ifdef PARENA_WITH_TLS
    int tls_handle = -1;
#endif
    if (!req || !raw) goto done;
    const char *body = json_body ? json_body : "";
    int n = snprintf(req, REQ_MAX, "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n%s%s%sContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                     method, path, host, bearer && *bearer ? "Authorization: Bearer " : "", bearer && *bearer ? bearer : "",
                     bearer && *bearer ? "\r\n" : "", (int)strlen(body), body);
    if (n < 0 || n >= REQ_MAX) goto done;
    size_t total = 0;
    if (use_tls) {
#ifdef PARENA_WITH_TLS
        tls_handle = tls_connect_impl(host, port);
        if (tls_handle < 0) goto done;
        if (tls_write_impl(tls_handle, req) < 0) goto done;
        while (total < RAW_MAX - 1) {
            int r = tls_read_into_impl(tls_handle, raw + total, RAW_MAX - total);
            if (r < 0) break;   /* real error */
            if (r == 0) break;  /* idle timeout -- treat as "peer is done sending" */
            total += (size_t)r;
        }
#else
        goto done; /* this binary was not built with PARENA_WITH_TLS -- fail clean, never fall back to plaintext */
#endif
    } else {
        s = dw_connect(host, port);
        if (s == DW_BAD_SOCK) goto done;
        for (int off = 0; off < n;) { long w = dw_send(s, req + off, (size_t)(n - off)); if (w <= 0) goto done; off += (int)w; }
        while (total < RAW_MAX - 1) {
            if (wait_readable(s, timeout_ms) <= 0) break;
            long r = dw_recv(s, raw + total, RAW_MAX - 1 - total);
            if (r <= 0) break;
            total += (size_t)r;
        }
    }
    if (total == 0) goto done;
    raw[total] = 0;
    int st = 0;
    if (sscanf(raw, "HTTP/%*d.%*d %d", &st) != 1) goto done;
    *status = st;
    char *b = strstr(raw, "\r\n\r\n");
    if (!b) { if (resp_n) resp[0] = 0; rc = 0; goto done; }
    b += 4;
    if (strstr(raw, "Transfer-Encoding: chunked") || strstr(raw, "transfer-encoding: chunked")) {
        size_t o = 0; char *q = b;
        while (*q) {
            unsigned long len = strtoul(q, &q, 16);
            while (*q && *q != '\n') q++;
            if (*q) q++;
            if (len == 0) break;
            for (unsigned long i = 0; i < len && *q && o + 1 < resp_n; i++) resp[o++] = *q++;
            if (*q == '\r') q++;
            if (*q == '\n') q++;
        }
        if (resp_n) resp[o] = 0;
    } else if (resp_n) {
        strncpy(resp, b, resp_n - 1); resp[resp_n - 1] = 0;
    }
    rc = 0;
done:
    if (s != DW_BAD_SOCK) dw_close(s);
#ifdef PARENA_WITH_TLS
    if (tls_handle >= 0) tls_close_impl(tls_handle);
#endif
    free(req); free(raw);
    return rc;
}
