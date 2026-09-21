/* DEADWEIGHT wire protocol v3 codec (docs/WIRE_PROTOCOL.md). Little-endian, explicit byte ops (no struct
 * punning, no alignment assumptions). Frame = u16 len (bytes after this field) + u8 type + payload. */
#ifndef DW_PROTOCOL_H
#define DW_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define DW_PROTO_VERSION 3   /* v3 = the 105-card Offense/Operations/Defense catalog (wire format unchanged from v2; old clients can't play the new cards) */
#define DW_MAX_FRAME_LEN 1024     /* max value of the u16 len field (only AUTH exceeds 256) */
#define DW_MAX_AUTH_TOKEN 900     /* IDUNA ES256 JWTs are ~400-500 bytes: too big for HELLO's 200-byte token */
#define DW_MAX_TOKEN 200
#define DW_NAME_LEN 16

enum {
    DW_C_HELLO = 0x01, DW_C_QUEUE = 0x02, DW_C_PLAY = 0x03, DW_C_LEAVE = 0x04, DW_C_PING = 0x05, DW_C_AUTH = 0x06, DW_C_DRAFT_PICK = 0x07,
    DW_C_DRAFT_RESUME = 0x08, /* S510: submit an already-drafted (IDUNA-persisted) deck instead of redrafting -- see dw_draft_deck_valid */
    DW_S_WELCOME = 0x81, DW_S_QUEUED = 0x82, DW_S_MATCH_FOUND = 0x83, DW_S_ROUND_START = 0x84,
    DW_S_PLAY_ACK = 0x85, DW_S_PLAY_REJECT = 0x86, DW_S_ROUND_RESULT = 0x87, DW_S_MATCH_END = 0x88,
    DW_S_PONG = 0x89, DW_S_DRAFT_OFFER = 0x8A, DW_S_DRAFT_DONE = 0x8B, DW_S_ERROR = 0x8F
};
/* DW_MODE_CARD is the "random" queue (whole shuffled catalog); DW_MODE_DRAFT drafts a 23-card deck first (core/draft.h) and
 * has its own queue. Backpack is VS1. */
enum { DW_MODE_CARD = 0, DW_MODE_BACKPACK = 1, DW_MODE_DRAFT = 2 };
#define DW_DRAFT_DECK 23
enum { DW_KIND_HUMAN = 0, DW_KIND_BOT = 1 };
enum { DW_FLAG_FAST_FORWARD = 1, DW_FLAG_AUTH_REQUIRED = 2 };
enum { DW_REJ_ILLEGAL_CARD = 1, DW_REJ_BAD_SLOT = 2, DW_REJ_WRONG_ROUND = 3, DW_REJ_ALREADY_LOCKED = 4, DW_REJ_NO_MATCH = 5 };
enum { DW_RES_LOSS = 0, DW_RES_WIN = 1, DW_RES_DRAW = 2 };
enum { DW_END_HULL = 0, DW_END_ROUNDS = 1, DW_END_FORFEIT = 2, DW_END_SERVER = 3, DW_END_BANKRUPT = 4 };
/* wire sentinels for the opponent's energy/armor (u8) and credits (i8) while Merkle Blindness hides them */
#define DW_HIDDEN_U8 255
#define DW_HIDDEN_I8 (-128)
enum { DW_ERR_BAD_PROTO = 1, DW_ERR_AUTH = 2, DW_ERR_BAD_FRAME = 3, DW_ERR_BAD_STATE = 4 };

typedef struct {
    uint8_t type;
    union {
        struct { uint8_t proto, mode, kind; char name[DW_NAME_LEN + 1]; uint8_t token_len; uint8_t token[DW_MAX_TOKEN]; } hello;
        struct { uint16_t token_len; uint8_t token[DW_MAX_AUTH_TOKEN]; } auth;
        struct { uint32_t match_id; uint8_t round; int8_t slot; } play;
        struct { uint8_t same_deck; } queue;         /* C_QUEUE, optional 1-byte payload (draft: 1 = replay last deck, 0 = redraft) */
        struct { uint8_t index, mult; } draft_pick;  /* index 0/1 into the offer, mult 1..3 */
        struct { uint8_t pick_no, total; int8_t card[2]; uint8_t left[3]; } draft_offer;   /* left = 1-of/2-of/3-of buckets remaining */
        struct { uint32_t deck_id; int8_t cards[DW_DRAFT_DECK]; } draft_done;
        struct { int8_t cards[DW_DRAFT_DECK]; } draft_resume;   /* C_DRAFT_RESUME */
        struct { uint32_t nonce; } ping;             /* PING and PONG */
        struct { uint32_t session_id; uint8_t flags; } welcome;
        struct { uint16_t waiting; } queued;
        struct { uint32_t match_id, seed; uint8_t seat; char opp_name[DW_NAME_LEN + 1]; uint8_t opp_kind; } match_found;
        struct { uint8_t round; int8_t hull_you, hull_opp; uint8_t energy_you, energy_opp; int8_t hand[4];
                 uint8_t opp_hand_size; uint16_t deadline_ms;
                 uint8_t armor_you, armor_opp; int8_t vault_you, vault_opp; uint8_t lock_mask, status_you, status_opp; } round_start;
        struct { uint32_t match_id; uint8_t round; } ack;
        struct { uint32_t match_id; uint8_t round, reason; } reject;
        struct { uint8_t round; int8_t card_you, card_opp; uint8_t dmg_you, dmg_opp; int8_t hull_you, hull_opp;
                 int8_t eff_you, eff_opp; uint8_t armor_you, armor_opp; int8_t vault_you, vault_opp;
                 uint8_t heal_you, heal_opp, roll_you, roll_opp, flags_you, flags_opp; } round_result;
        struct { uint32_t match_id; uint8_t result, reason; } match_end;
        struct { uint8_t code; } error;
    } u;
} DwMsg;

/* Encode one message. Returns total bytes written (incl. the 2-byte len), or -1 on bad input / small buffer. */
int dw_encode(const DwMsg *m, uint8_t *buf, size_t cap);
/* Decode one frame from buf[0..len). Returns 1 = decoded (*consumed set), 0 = need more bytes, -1 = malformed
 * (oversized len, unknown type, payload size mismatch, invalid token_len). After -1 the connection must close. */
int dw_decode(const uint8_t *buf, size_t len, DwMsg *out, size_t *consumed);
#endif
