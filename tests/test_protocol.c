/* Protocol codec tests: round trips for every message, byte-exact layout spot checks, malformed frames, and a
 * fuzz pass (random bytes must never crash / overread under ASan+UBSan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void roundtrip(const DwMsg *m) {
    uint8_t b[512]; DwMsg d; size_t used = 0;
    int n = dw_encode(m, b, sizeof b);
    CHECK(n > 2);
    if (n <= 2) return;
    /* every strict prefix needs more bytes, never errors */
    for (int k = 0; k < n; k++) { int r = dw_decode(b, (size_t)k, &d, &used); CHECK(r == 0); }
    CHECK(dw_decode(b, (size_t)n, &d, &used) == 1);
    CHECK(used == (size_t)n);
    uint8_t b2[512];
    int n2 = dw_encode(&d, b2, sizeof b2);
    CHECK(n2 == n && memcmp(b, b2, (size_t)n) == 0);
    /* trailing bytes of the next frame are not consumed */
    uint8_t b3[600]; memcpy(b3, b, (size_t)n); memcpy(b3 + n, b, (size_t)n);
    CHECK(dw_decode(b3, (size_t)n * 2, &d, &used) == 1 && used == (size_t)n);
    /* undersized output buffer is rejected, not overrun */
    CHECK(dw_encode(m, b2, (size_t)n - 1) == -1);
}

int main(void) {
    DwMsg m; uint8_t b[512]; DwMsg d; size_t used;
    memset(&m, 0, sizeof m); m.type = DW_C_HELLO; m.u.hello.proto = 1; m.u.hello.mode = 0; m.u.hello.kind = 1;
    strcpy(m.u.hello.name, "Ripper"); m.u.hello.token_len = 3; memcpy(m.u.hello.token, "abc", 3);
    roundtrip(&m);
    int n = dw_encode(&m, b, sizeof b);
    CHECK(n == 2 + 1 + 20 + 3 && b[0] == 24);
    CHECK(b[1] == 0 && b[2] == DW_C_HELLO && b[3] == 1 && b[6] == 'R');
    m.u.hello.token_len = 0; roundtrip(&m);
    m.u.hello.token_len = DW_MAX_TOKEN; memset(m.u.hello.token, 0xAB, DW_MAX_TOKEN); roundtrip(&m);
    memset(m.u.hello.name, 'x', DW_NAME_LEN); m.u.hello.name[DW_NAME_LEN] = 0; roundtrip(&m);
    m.u.hello.token_len = 201; CHECK(dw_encode(&m, b, sizeof b) == -1);

    memset(&m, 0, sizeof m); m.type = DW_C_QUEUE; roundtrip(&m);
    m.type = DW_C_LEAVE; roundtrip(&m);
    m.type = DW_C_PLAY; m.u.play.match_id = 0xDEADBEEF; m.u.play.round = 7; m.u.play.slot = -1; roundtrip(&m);
    n = dw_encode(&m, b, sizeof b);
    CHECK(n == 9 && b[0] == 7 && b[2] == 3 && b[3] == 0xEF && b[6] == 0xDE && b[7] == 7 && b[8] == 0xFF);
    m.type = DW_C_PING; m.u.ping.nonce = 0x01020304; roundtrip(&m);
    m.type = DW_S_PONG; roundtrip(&m);
    m.type = DW_S_WELCOME; m.u.welcome.session_id = 99; m.u.welcome.flags = 3; roundtrip(&m);
    m.type = DW_S_QUEUED; m.u.queued.waiting = 513; roundtrip(&m);
    m.type = DW_S_MATCH_FOUND; m.u.match_found.match_id = 5; m.u.match_found.seed = 0xFFFFFFFF; m.u.match_found.seat = 1;
    strcpy(m.u.match_found.opp_name, "Wall"); m.u.match_found.opp_kind = 1; roundtrip(&m);
    m.type = DW_S_ROUND_START; m.u.round_start.round = 3; m.u.round_start.hull_you = 20; m.u.round_start.hull_opp = -3;
    m.u.round_start.energy_you = 4; m.u.round_start.energy_opp = 6; m.u.round_start.hand[0] = 8; m.u.round_start.hand[3] = -1;
    m.u.round_start.opp_hand_size = 4; m.u.round_start.deadline_ms = 20000; roundtrip(&m);
    n = dw_encode(&m, b, sizeof b); CHECK(n == 15 && b[0] == 13 && b[2] == DW_S_ROUND_START);
    m.type = DW_S_PLAY_ACK; m.u.ack.match_id = 1; m.u.ack.round = 2; roundtrip(&m);
    m.type = DW_S_PLAY_REJECT; m.u.reject.match_id = 1; m.u.reject.round = 2; m.u.reject.reason = 4; roundtrip(&m);
    m.type = DW_S_ROUND_RESULT; m.u.round_result.round = 8; m.u.round_result.card_you = -1; m.u.round_result.card_opp = 8;
    m.u.round_result.dmg_you = 10; m.u.round_result.dmg_opp = 0; m.u.round_result.hull_you = -7; m.u.round_result.hull_opp = 20; roundtrip(&m);
    m.type = DW_S_MATCH_END; m.u.match_end.match_id = 77; m.u.match_end.result = 2; m.u.match_end.reason = 1; roundtrip(&m);
    m.type = DW_S_ERROR; m.u.error.code = 3; roundtrip(&m);
    m.type = 0x55; CHECK(dw_encode(&m, b, sizeof b) == -1);

    /* malformed frames */
    uint8_t bad1[] = { 0, 0 };                    CHECK(dw_decode(bad1, 2, &d, &used) == -1);   /* len 0 */
    uint8_t bad2[] = { 0x01, 0x01, 0x02 };        CHECK(dw_decode(bad2, 3, &d, &used) == -1);   /* len 257 > max */
    uint8_t bad3[] = { 1, 0, 0x77 };              CHECK(dw_decode(bad3, 3, &d, &used) == -1);   /* unknown type */
    uint8_t bad4[] = { 2, 0, DW_C_QUEUE, 0 };     CHECK(dw_decode(bad4, 4, &d, &used) == -1);   /* QUEUE with payload */
    uint8_t bad5[] = { 3, 0, DW_C_PLAY, 0, 0 };   CHECK(dw_decode(bad5, 5, &d, &used) == -1);   /* short PLAY */
    uint8_t bad6[2 + 1 + 20]; memset(bad6, 0, sizeof bad6); bad6[0] = 21; bad6[2] = DW_C_HELLO; bad6[22] = 5; /* token_len 5, none present */
    CHECK(dw_decode(bad6, sizeof bad6, &d, &used) == -1);
    uint8_t bad7[2 + 1 + 19]; memset(bad7, 0, sizeof bad7); bad7[0] = 20; bad7[2] = DW_C_HELLO; /* HELLO shorter than fixed part */
    CHECK(dw_decode(bad7, sizeof bad7, &d, &used) == -1);
    uint8_t bad8[2 + 1 + 20 + 201]; memset(bad8, 0, sizeof bad8); bad8[0] = 222 & 255; bad8[2] = DW_C_HELLO; bad8[22] = 201;
    CHECK(dw_decode(bad8, sizeof bad8, &d, &used) == -1);

    /* fuzz: random bytes, random truncations of valid frames, single-bit flips of valid frames */
    for (int i = 0; i < 200000; i++) {
        uint8_t f[300]; size_t l = rnd() % 300;
        for (size_t k = 0; k < l; k++) f[k] = (uint8_t)rnd();
        if (l >= 2 && (rnd() & 1)) { f[0] = (uint8_t)(rnd() % 40); f[1] = 0; if (l > 2) f[2] = (uint8_t)((rnd() & 1) ? (0x80 | (rnd() % 16)) : (rnd() % 8)); }
        int r = dw_decode(f, l, &d, &used);
        CHECK(r >= -1 && r <= 1);
        if (r == 1) { CHECK(used >= 3 && used <= l); uint8_t o[512]; CHECK(dw_encode(&d, o, sizeof o) >= 3); }
    }
    for (int i = 0; i < 20000; i++) {
        memset(&m, 0, sizeof m);
        static const uint8_t types[] = { DW_C_HELLO, DW_C_PLAY, DW_S_MATCH_FOUND, DW_S_ROUND_START, DW_S_ROUND_RESULT, DW_S_MATCH_END };
        m.type = types[rnd() % 6];
        for (size_t k = 0; k < sizeof m.u; k++) ((uint8_t *)&m.u)[k] = (uint8_t)rnd();
        if (m.type == DW_C_HELLO) { m.u.hello.token_len = (uint8_t)(rnd() % 201); m.u.hello.name[DW_NAME_LEN] = 0; }
        if (m.type == DW_S_MATCH_FOUND) m.u.match_found.opp_name[DW_NAME_LEN] = 0;
        int en = dw_encode(&m, b, sizeof b);
        CHECK(en > 0);
        if (en > 0) { b[rnd() % (unsigned)en] ^= (uint8_t)(1u << (rnd() % 8)); int r = dw_decode(b, (size_t)en, &d, &used); CHECK(r >= -1 && r <= 1); }
    }
    printf("test_protocol: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
