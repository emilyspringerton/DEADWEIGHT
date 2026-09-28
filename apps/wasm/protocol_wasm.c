/* Real wasm export boundary around DEADWEIGHT's actual wire codec (core/protocol.c, compiled
 * into this same module completely unmodified -- "eat the codebase," not reimplement it a third
 * time the way web/src/proto.ts had to). Every message type in docs/WIRE_PROTOCOL.md gets a
 * named setter (client -> server) or getter (server -> client) group, built directly against
 * DwMsg's real field names -- never raw memory-offset poking from JS, which would silently break
 * on any struct-layout/padding change (found the hard way while proving this out: see
 * docs/NATIVE_WASM_CLIENT_NORTHSTAR.md's own "first attempt" note).
 *
 * One shared g_msg / g_encode_buf / g_decode_in triple: this module encodes or decodes exactly
 * one message at a time, same single-message-at-a-time shape core/client.c's own dwc_send/
 * dwc_recv already use. */
#include "protocol.h"

static DwMsg g_msg;
static uint8_t g_encode_buf[DW_MAX_FRAME_LEN + 64];
static uint8_t g_decode_in[DW_MAX_FRAME_LEN + 64];

uint8_t *encode_buf_ptr(void) { return g_encode_buf; }
uint8_t *decode_in_ptr(void) { return g_decode_in; }
int wasm_encode(void) { return dw_encode(&g_msg, g_encode_buf, sizeof g_encode_buf); }
int wasm_decode(int len) { size_t used = 0; return dw_decode(g_decode_in, (size_t)len, &g_msg, &used); }
uint8_t get_msg_type(void) { return g_msg.type; }

/* ================= client -> server (setters + encode) ================= */

void set_hello(uint8_t proto, uint8_t mode, uint8_t kind, uint8_t token_len) {
    g_msg.type = DW_C_HELLO;
    g_msg.u.hello.proto = proto; g_msg.u.hello.mode = mode; g_msg.u.hello.kind = kind;
    g_msg.u.hello.token_len = token_len;
}
void set_hello_name_char(int i, uint8_t c) { g_msg.u.hello.name[i] = (char)c; }
void set_hello_token_byte(int i, uint8_t b) { g_msg.u.hello.token[i] = b; }
/* Getters too (not just setters): a real client re-decodes its own just-sent HELLO in the round-
 * trip test below, and nothing stops a future caller from wanting to inspect an already-built
 * outgoing message either. */
uint8_t get_hello_proto(void) { return g_msg.u.hello.proto; }
uint8_t get_hello_mode(void) { return g_msg.u.hello.mode; }
uint8_t get_hello_kind(void) { return g_msg.u.hello.kind; }
uint8_t get_hello_token_len(void) { return g_msg.u.hello.token_len; }
uint8_t get_hello_name_char(int i) { return (uint8_t)g_msg.u.hello.name[i]; }
uint8_t get_hello_token_byte(int i) { return g_msg.u.hello.token[i]; }

void set_queue(uint8_t same_deck, uint8_t has_match_token) {
    g_msg.type = DW_C_QUEUE;
    g_msg.u.queue.same_deck = same_deck; g_msg.u.queue.has_match_token = has_match_token;
}
void set_queue_token_char(int i, uint8_t c) { g_msg.u.queue.match_token[i] = (char)c; }

void set_play(uint32_t match_id, uint8_t round, int8_t slot) {
    g_msg.type = DW_C_PLAY;
    g_msg.u.play.match_id = match_id; g_msg.u.play.round = round; g_msg.u.play.slot = slot;
}

void set_leave(void) { g_msg.type = DW_C_LEAVE; }

void set_ping(uint32_t nonce) { g_msg.type = DW_C_PING; g_msg.u.ping.nonce = nonce; }

void set_auth(uint16_t token_len) { g_msg.type = DW_C_AUTH; g_msg.u.auth.token_len = token_len; }
void set_auth_token_byte(int i, uint8_t b) { g_msg.u.auth.token[i] = b; }

void set_draft_pick(uint8_t index, uint8_t mult) {
    g_msg.type = DW_C_DRAFT_PICK;
    g_msg.u.draft_pick.index = index; g_msg.u.draft_pick.mult = mult;
}

void set_draft_resume(void) { g_msg.type = DW_C_DRAFT_RESUME; }
void set_draft_resume_card(int i, int8_t c) { g_msg.u.draft_resume.cards[i] = c; }

/* ================= server -> client (getters, after wasm_decode) ================= */

uint32_t get_welcome_session_id(void) { return g_msg.u.welcome.session_id; }
uint8_t get_welcome_flags(void) { return g_msg.u.welcome.flags; }

uint16_t get_queued_waiting(void) { return g_msg.u.queued.waiting; }

uint32_t get_match_found_match_id(void) { return g_msg.u.match_found.match_id; }
uint32_t get_match_found_seed(void) { return g_msg.u.match_found.seed; }
uint8_t get_match_found_seat(void) { return g_msg.u.match_found.seat; }
uint8_t get_match_found_opp_kind(void) { return g_msg.u.match_found.opp_kind; }
uint8_t get_match_found_opp_name_char(int i) { return (uint8_t)g_msg.u.match_found.opp_name[i]; }

uint8_t get_round_start_round(void) { return g_msg.u.round_start.round; }
int8_t get_round_start_hull_you(void) { return g_msg.u.round_start.hull_you; }
int8_t get_round_start_hull_opp(void) { return g_msg.u.round_start.hull_opp; }
uint8_t get_round_start_energy_you(void) { return g_msg.u.round_start.energy_you; }
uint8_t get_round_start_energy_opp(void) { return g_msg.u.round_start.energy_opp; }
int8_t get_round_start_hand(int i) { return g_msg.u.round_start.hand[i]; }
uint8_t get_round_start_opp_hand_size(void) { return g_msg.u.round_start.opp_hand_size; }
uint16_t get_round_start_deadline_ms(void) { return g_msg.u.round_start.deadline_ms; }
uint8_t get_round_start_armor_you(void) { return g_msg.u.round_start.armor_you; }
uint8_t get_round_start_armor_opp(void) { return g_msg.u.round_start.armor_opp; }
int8_t get_round_start_vault_you(void) { return g_msg.u.round_start.vault_you; }
int8_t get_round_start_vault_opp(void) { return g_msg.u.round_start.vault_opp; }
uint8_t get_round_start_lock_mask(void) { return g_msg.u.round_start.lock_mask; }
uint8_t get_round_start_status_you(void) { return g_msg.u.round_start.status_you; }
uint8_t get_round_start_status_opp(void) { return g_msg.u.round_start.status_opp; }

uint32_t get_play_ack_match_id(void) { return g_msg.u.ack.match_id; }
uint8_t get_play_ack_round(void) { return g_msg.u.ack.round; }

uint32_t get_play_reject_match_id(void) { return g_msg.u.reject.match_id; }
uint8_t get_play_reject_round(void) { return g_msg.u.reject.round; }
uint8_t get_play_reject_reason(void) { return g_msg.u.reject.reason; }

uint8_t get_round_result_round(void) { return g_msg.u.round_result.round; }
int8_t get_round_result_card_you(void) { return g_msg.u.round_result.card_you; }
int8_t get_round_result_card_opp(void) { return g_msg.u.round_result.card_opp; }
uint8_t get_round_result_dmg_you(void) { return g_msg.u.round_result.dmg_you; }
uint8_t get_round_result_dmg_opp(void) { return g_msg.u.round_result.dmg_opp; }
int8_t get_round_result_hull_you(void) { return g_msg.u.round_result.hull_you; }
int8_t get_round_result_hull_opp(void) { return g_msg.u.round_result.hull_opp; }
int8_t get_round_result_eff_you(void) { return g_msg.u.round_result.eff_you; }
int8_t get_round_result_eff_opp(void) { return g_msg.u.round_result.eff_opp; }
uint8_t get_round_result_armor_you(void) { return g_msg.u.round_result.armor_you; }
uint8_t get_round_result_armor_opp(void) { return g_msg.u.round_result.armor_opp; }
int8_t get_round_result_vault_you(void) { return g_msg.u.round_result.vault_you; }
int8_t get_round_result_vault_opp(void) { return g_msg.u.round_result.vault_opp; }
uint8_t get_round_result_heal_you(void) { return g_msg.u.round_result.heal_you; }
uint8_t get_round_result_heal_opp(void) { return g_msg.u.round_result.heal_opp; }
uint8_t get_round_result_roll_you(void) { return g_msg.u.round_result.roll_you; }
uint8_t get_round_result_roll_opp(void) { return g_msg.u.round_result.roll_opp; }
uint8_t get_round_result_flags_you(void) { return g_msg.u.round_result.flags_you; }
uint8_t get_round_result_flags_opp(void) { return g_msg.u.round_result.flags_opp; }

uint32_t get_match_end_match_id(void) { return g_msg.u.match_end.match_id; }
uint8_t get_match_end_result(void) { return g_msg.u.match_end.result; }
uint8_t get_match_end_reason(void) { return g_msg.u.match_end.reason; }

uint32_t get_pong_nonce(void) { return g_msg.u.ping.nonce; }

uint8_t get_draft_offer_pick_no(void) { return g_msg.u.draft_offer.pick_no; }
uint8_t get_draft_offer_total(void) { return g_msg.u.draft_offer.total; }
int8_t get_draft_offer_card(int i) { return g_msg.u.draft_offer.card[i]; }
uint8_t get_draft_offer_left(int i) { return g_msg.u.draft_offer.left[i]; }

uint32_t get_draft_done_deck_id(void) { return g_msg.u.draft_done.deck_id; }
int8_t get_draft_done_card(int i) { return g_msg.u.draft_done.cards[i]; }

uint8_t get_error_code(void) { return g_msg.u.error.code; }
