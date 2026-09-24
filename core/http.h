/* Minimal blocking HTTP/1.1 client + tiny JSON field helpers, for talking to IDUNA. Originally
 * plain-HTTP-only (same-box, trusted responses -- same scope decision as ECOWAR's own
 * http_client.h), extended with real TLS support (S508e, founder real-time: a shipped client
 * reaching the public internet over plaintext HTTP is a real shipping bug, and IDUNA's own real
 * production URL is https://) via PARENA's net/tls.prn -- a real mbedTLS FFI binding, never
 * hand-rolled crypto. See core/http.c's own doc comment on PARENA_WITH_TLS for the build-time
 * opt-in story. */
#ifndef DW_HTTP_H
#define DW_HTTP_H
#include <stddef.h>
/* Returns 0 with *status/resp filled (body NUL-terminated, truncated to resp_n-1), -1 on any
 * socket-level failure. use_tls=1 requires this binary to have been built with PARENA_WITH_TLS
 * (and linked against PARENA's runtime + mbedTLS) -- without that, a use_tls=1 call fails clean
 * (-1), it never silently falls back to plaintext. */
int dw_http(const char *method, const char *host, int port, int use_tls, const char *path, const char *bearer,
            const char *json_body, char *resp, size_t resp_n, int *status, int timeout_ms);
/* Extract a simple (escape-free) JSON string value: "key":"value". 1 if found and fits, else 0. */
int dw_json_str(const char *json, const char *key, char *out, size_t n);
/* Extract a JSON integer/bool value: "key":123 or "key":true/false (true/false -> 1/0). 1 if found, else 0. */
int dw_json_int(const char *json, const char *key, int *out);
/* Extract a flat JSON array of non-negative small ints: "key":[1,2,3]. Returns how many were
 * written into out (capped at max_n), 0 if the key wasn't found. */
int dw_json_int_array(const char *json, const char *key, int *out, int max_n);
/* Points *out_obj at the start of the (0-based) idx'th object inside a JSON array of objects
 * ("key":[{...},{...}]) -- NOT a copy, NOT null-terminated at the object boundary, just a pointer
 * into the original json buffer. Pass it straight to dw_json_str/dw_json_int to read that one
 * object's own fields: since object idx's own fields always appear before any later sibling
 * object's same-named field, a forward strstr from this pointer finds the right one. key may be
 * NULL for a bare top-level array response ("[{...},{...}]" with no wrapping object at all).
 * Same "escape-free" scope as the rest of this header (quote-tracked only enough to not miscount
 * a brace inside a string value). Returns 1 if idx exists, 0 if the array ends first or the key
 * wasn't found at all. */
int dw_json_array_at(const char *json, const char *key, int idx, const char **out_obj);
/* Parse http://host[:port][/...] or https://host[:port][/...] -> host/port/use_tls (1 for
 * https, 0 for http; default port 443/80 respectively). 0 ok, -1 unrecognized scheme. */
int dw_parse_url(const char *url, char *host, size_t hn, int *port, int *use_tls);
#endif
