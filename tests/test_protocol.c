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
    uint8_t b[1200]; DwMsg d; size_t used = 0;
    int n = dw_encode(m, b, sizeof b);
    CHECK(n > 2);
    if (n <= 2) return;
    /* every strict prefix needs more bytes, never errors */
    for (int k = 0; k < n; k++) { int r = dw_decode(b, (size_t)k, &d, &used); CHECK(r == 0); }
    CHECK(dw_decode(b, (size_t)n, &d, &used) == 1);
    CHECK(used == (size_t)n);
    uint8_t b2[1200];
    int n2 = dw_encode(&d, b2, sizeof b2);
    CHECK(n2 == n && memcmp(b, b2, (size_t)n) == 0);
    /* trailing bytes of the next frame are not consumed */
    uint8_t b3[2400]; memcpy(b3, b, (size_t)n); memcpy(b3 + n, b, (size_t)n);
    CHECK(dw_decode(b3, (size_t)n * 2, &d, &used) == 1 && used == (size_t)n);
    /* undersized output buffer is rejected, not overrun */
    CHECK(dw_encode(m, b2, (size_t)n - 1) == -1);
}

int main(void) {
    DwMsg m; uint8_t b[1200]; DwMsg d; size_t used;
    memset(&m, 0, sizeof m); m.type = DW_C_HELLO; m.u.hello.proto = DW_PROTO_VERSION; m.u.hello.mode = 0; m.u.hello.kind = 1;
    strcpy(m.u.hello.name, "Ripper"); m.u.hello.token_len = 3; memcpy(m.u.hello.token, "abc", 3);
    roundtrip(&m);
    int n = dw_encode(&m, b, sizeof b);
    CHECK(n == 2 + 1 + 20 + 3 && b[0] == 24);
    CHECK(b[1] == 0 && b[2] == DW_C_HELLO && b[3] == DW_PROTO_VERSION && b[6] == 'R');
    m.u.hello.token_len = 0; roundtrip(&m);
    m.u.hello.token_len = DW_MAX_TOKEN; memset(m.u.hello.token, 0xAB, DW_MAX_TOKEN); roundtrip(&m);
    memset(m.u.hello.name, 'x', DW_NAME_LEN); m.u.hello.name[DW_NAME_LEN] = 0; roundtrip(&m);
    m.u.hello.token_len = 201; CHECK(dw_encode(&m, b, sizeof b) == -1);

    memset(&m, 0, sizeof m); m.type = DW_C_QUEUE; roundtrip(&m);
    m.type = DW_C_LEAVE; roundtrip(&m);
    m.type = DW_C_PLAY; m.u.play.match_id = 0xDEADBEEF; m.u.play.round = 7; m.u.play.slot = -1; roundtrip(&m);
    n = dw_encode(&m, b, sizeof b);
    CHECK(n == 9 && b[0] == 7 && b[2] == 3 && b[3] == 0xEF && b[6] == 0xDE && b[7] == 7 && b[8] == 0xFF);
    m.type = DW_C_AUTH; m.u.auth.token_len = 0; roundtrip(&m);
    m.u.auth.token_len = 5; memcpy(m.u.auth.token, "eyJhb", 5); roundtrip(&m);
    n = dw_encode(&m, b, sizeof b); CHECK(n == 2 + 1 + 2 + 5 && b[0] == 8 && b[2] == DW_C_AUTH && b[3] == 5 && b[4] == 0 && b[5] == 'e');
    m.u.auth.token_len = DW_MAX_AUTH_TOKEN; memset(m.u.auth.token, 'x', DW_MAX_AUTH_TOKEN);
    { uint8_t big[1100]; DwMsg dd; size_t uu; int nn = dw_encode(&m, big, sizeof big); CHECK(nn == 2 + 1 + 2 + DW_MAX_AUTH_TOKEN); CHECK(dw_decode(big, (size_t)nn, &dd, &uu) == 1 && dd.u.auth.token_len == DW_MAX_AUTH_TOKEN); }
    m.u.auth.token_len = DW_MAX_AUTH_TOKEN + 1; CHECK(dw_encode(&m, b, sizeof b) == -1);
    m.type = DW_C_PING; m.u.ping.nonce = 0x01020304; roundtrip(&m);
    m.type = DW_S_PONG; roundtrip(&m);
    m.type = DW_S_WELCOME; m.u.welcome.session_id = 99; m.u.welcome.flags = 3; roundtrip(&m);
    m.type = DW_S_QUEUED; m.u.queued.waiting = 513; roundtrip(&m);
    m.type = DW_S_MATCH_FOUND; m.u.match_found.match_id = 5; m.u.match_found.seed = 0xFFFFFFFF; m.u.match_found.seat = 1;
    strcpy(m.u.match_found.opp_name, "Wall"); m.u.match_found.opp_kind = 1; roundtrip(&m);
    m.type = DW_S_ROUND_START; m.u.round_start.round = 3; m.u.round_start.hull_you = 20; m.u.round_start.hull_opp = -3;
    m.u.round_start.energy_you = 4; m.u.round_start.energy_opp = 6; m.u.round_start.hand[0] = 8; m.u.round_start.hand[3] = -1;
    m.u.round_start.opp_hand_size = 4; m.u.round_start.deadline_ms = 20000;
    m.u.round_start.armor_you = 5; m.u.round_start.armor_opp = DW_HIDDEN_U8; m.u.round_start.vault_you = -4; m.u.round_start.vault_opp = DW_HIDDEN_I8;
    m.u.round_start.lock_mask = 0x0A; m.u.round_start.status_you = 3; m.u.round_start.status_opp = 4; roundtrip(&m);
    n = dw_encode(&m, b, sizeof b); CHECK(n == 22 && b[0] == 20 && b[2] == DW_S_ROUND_START);
    m.type = DW_S_PLAY_ACK; m.u.ack.match_id = 1; m.u.ack.round = 2; roundtrip(&m);
    m.type = DW_S_PLAY_REJECT; m.u.reject.match_id = 1; m.u.reject.round = 2; m.u.reject.reason = 4; roundtrip(&m);
    m.type = DW_S_ROUND_RESULT; m.u.round_result.round = 8; m.u.round_result.card_you = -1; m.u.round_result.card_opp = 8;
    m.u.round_result.dmg_you = 10; m.u.round_result.dmg_opp = 0; m.u.round_result.hull_you = -7; m.u.round_result.hull_opp = 20;
    m.u.round_result.eff_you = 35; m.u.round_result.eff_opp = -1; m.u.round_result.armor_you = 12; m.u.round_result.armor_opp = 0;
    m.u.round_result.vault_you = -6; m.u.round_result.vault_opp = 99; m.u.round_result.heal_you = 8; m.u.round_result.heal_opp = 2;
    m.u.round_result.roll_you = 73; m.u.round_result.roll_opp = 99; m.u.round_result.flags_you = 0xFF; m.u.round_result.flags_opp = 0x81; roundtrip(&m);
    n = dw_encode(&m, b, sizeof b); CHECK(n == 22 && b[0] == 20 && b[2] == DW_S_ROUND_RESULT);
    m.type = DW_S_MATCH_END; m.u.match_end.match_id = 77; m.u.match_end.result = 2; m.u.match_end.reason = 1; roundtrip(&m);
    m.type = DW_S_ERROR; m.u.error.code = 3; roundtrip(&m);
    /* draft mode */
    memset(&m, 0, sizeof m); m.type = DW_C_QUEUE; m.u.queue.same_deck = 1; roundtrip(&m);
    { uint8_t qb[8]; int qn = dw_encode(&m, qb, sizeof qb); CHECK(qn == 4); DwMsg qd; size_t qu; CHECK(dw_decode(qb, (size_t)qn, &qd, &qu) == 1 && qd.u.queue.same_deck == 1);
      memset(&m, 0, sizeof m); m.type = DW_C_QUEUE; qn = dw_encode(&m, qb, sizeof qb); CHECK(qn == 3); CHECK(dw_decode(qb, (size_t)qn, &qd, &qu) == 1 && qd.u.queue.same_deck == 0); }
    memset(&m, 0, sizeof m); m.type = DW_C_DRAFT_PICK; m.u.draft_pick.index = 1; m.u.draft_pick.mult = 3; roundtrip(&m);
    memset(&m, 0, sizeof m); m.type = DW_C_DRAFT_RESUME;
    for (int i = 0; i < DW_DRAFT_DECK; i++) m.u.draft_resume.cards[i] = (int8_t)(i % 17);
    roundtrip(&m);
    memset(&m, 0, sizeof m); m.type = DW_S_DRAFT_OFFER; m.u.draft_offer.pick_no = 7; m.u.draft_offer.total = 16; m.u.draft_offer.card[0] = 12; m.u.draft_offer.card[1] = 70;
    m.u.draft_offer.left[0] = 4; m.u.draft_offer.left[1] = 2; m.u.draft_offer.left[2] = 1; roundtrip(&m);
    memset(&m, 0, sizeof m); m.type = DW_S_DRAFT_DONE; m.u.draft_done.deck_id = 0xABCDEF01u; for (int i = 0; i < DW_DRAFT_DECK; i++) m.u.draft_done.cards[i] = (int8_t)(i * 3); roundtrip(&m);
    m.type = 0x55; CHECK(dw_encode(&m, b, sizeof b) == -1);

    /* malformed frames */
    uint8_t bad1[] = { 0, 0 };                    CHECK(dw_decode(bad1, 2, &d, &used) == -1);   /* len 0 */
    uint8_t bad2[] = { 0x01, 0x04, 0x02 };        CHECK(dw_decode(bad2, 3, &d, &used) == -1);   /* len 1025 > max */
    uint8_t bad9[] = { 2, 0, DW_C_AUTH, 0 };      CHECK(dw_decode(bad9, 4, &d, &used) == -1);   /* AUTH shorter than its length field */
    uint8_t bad10[] = { 4, 0, DW_C_AUTH, 5, 0, 'a' }; CHECK(dw_decode(bad10, 6, &d, &used) == -1);   /* AUTH len/payload mismatch */
    uint8_t bad3[] = { 1, 0, 0x77 };              CHECK(dw_decode(bad3, 3, &d, &used) == -1);   /* unknown type */
    uint8_t bad4[] = { 3, 0, DW_C_QUEUE, 0, 0 };  CHECK(dw_decode(bad4, 5, &d, &used) == -1);   /* QUEUE with a 2-byte payload (0 or 1 byte only) */
    uint8_t bad5[] = { 3, 0, DW_C_PLAY, 0, 0 };   CHECK(dw_decode(bad5, 5, &d, &used) == -1);   /* short PLAY */
    uint8_t bad6[2 + 1 + 20]; memset(bad6, 0, sizeof bad6); bad6[0] = 21; bad6[2] = DW_C_HELLO; bad6[22] = 5; /* token_len 5, none present */
    CHECK(dw_decode(bad6, sizeof bad6, &d, &used) == -1);
    uint8_t bad7[2 + 1 + 19]; memset(bad7, 0, sizeof bad7); bad7[0] = 20; bad7[2] = DW_C_HELLO; /* HELLO shorter than fixed part */
    CHECK(dw_decode(bad7, sizeof bad7, &d, &used) == -1);
    uint8_t bad8[2 + 1 + 20 + 201]; memset(bad8, 0, sizeof bad8); bad8[0] = 222 & 255; bad8[2] = DW_C_HELLO; bad8[22] = 201;
    CHECK(dw_decode(bad8, sizeof bad8, &d, &used) == -1);

    /* fuzz: random bytes, random truncations of valid frames, single-bit flips of valid frames */
    for (int i = 0; i < 200000; i++) {
        uint8_t f[1100]; size_t l = rnd() % 1100;
        for (size_t k = 0; k < l; k++) f[k] = (uint8_t)rnd();
        if (l >= 2 && (rnd() & 1)) { f[0] = (uint8_t)(rnd() % 40); f[1] = 0; if (l > 2) f[2] = (uint8_t)((rnd() & 1) ? (0x80 | (rnd() % 16)) : (rnd() % 8)); }
        if (l >= 5 && (rnd() % 4) == 0) { f[2] = DW_C_AUTH; f[0] = (uint8_t)(rnd() % 60); f[1] = 0; f[3] = (uint8_t)(f[0] >= 3 ? f[0] - 3 : 0); f[4] = 0; }
        int r = dw_decode(f, l, &d, &used);
        CHECK(r >= -1 && r <= 1);
        if (r == 1) { CHECK(used >= 3 && used <= l); uint8_t o[1200]; CHECK(dw_encode(&d, o, sizeof o) >= 3); }
    }
    for (int i = 0; i < 20000; i++) {
        memset(&m, 0, sizeof m);
        static const uint8_t types[] = { DW_C_HELLO, DW_C_AUTH, DW_C_PLAY, DW_S_MATCH_FOUND, DW_S_ROUND_START, DW_S_ROUND_RESULT, DW_S_MATCH_END };
        m.type = types[rnd() % 7];
        for (size_t k = 0; k < sizeof m.u; k++) ((uint8_t *)&m.u)[k] = (uint8_t)rnd();
        if (m.type == DW_C_AUTH) m.u.auth.token_len = (uint16_t)(rnd() % 901);
        if (m.type == DW_C_HELLO) { m.u.hello.token_len = (uint8_t)(rnd() % 201); m.u.hello.name[DW_NAME_LEN] = 0; }
        if (m.type == DW_S_MATCH_FOUND) m.u.match_found.opp_name[DW_NAME_LEN] = 0;
        int en = dw_encode(&m, b, sizeof b);
        CHECK(en > 0);
        if (en > 0) { b[rnd() % (unsigned)en] ^= (uint8_t)(1u << (rnd() % 8)); int r = dw_decode(b, (size_t)en, &d, &used); CHECK(r >= -1 && r <= 1); }
    }
    printf("test_protocol: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
