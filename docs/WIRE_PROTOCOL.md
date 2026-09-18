# DEADWEIGHT wire protocol v1 (TCP)

Little-endian. Frame = `u16 len` (bytes after this field) + `u8 type` + payload. Max `len` 1024 (only AUTH is ever larger than 256); longer =
protocol violation, server closes. Strings are fixed 16-byte NUL-padded UTF-8 (`name16`).
One TCP connection = one session. Server sends nothing before HELLO. `proto` = 1.

## Client → server

| type | name | payload |
|---|---|---|
| 0x01 | HELLO | `u8 proto`, `u8 mode` (0=card, 1=backpack reserved), `u8 kind` (0=human, 1=bot), `name16 name`, `u8 token_len`, `token[token_len ≤ 200]` (IDUNA player token; empty allowed only with `--no-auth`) |
| 0x02 | QUEUE | — |
| 0x03 | PLAY | `u32 match_id`, `u8 round`, `i8 slot` (0–3, −1 = pass) |
| 0x04 | LEAVE | — (leaves queue or forfeits the current match) |
| 0x05 | PING | `u32 nonce` |
| 0x06 | AUTH | `u16 token_len` (≤ 900), `token[token_len]` — the IDUNA token (guest player JWT, or DEADWEIGHT-BOTS agent JWT for bots). **Real IDUNA ES256 JWTs are ~400–500 bytes, far above HELLO's 200-byte `token` field**, so with auth required the client sends HELLO with `token_len = 0` and then AUTH; the server replies WELCOME only after IDUNA verification. HELLO's inline token (≤200) is still accepted for short tokens. With `--no-auth` the server sends WELCOME straight after HELLO and ignores a later AUTH. |

## Server → client

| type | name | payload |
|---|---|---|
| 0x81 | WELCOME | `u32 session_id`, `u8 server_flags` (bit0 = fast_forward, bit1 = auth_required) |
| 0x82 | QUEUED | `u16 waiting` |
| 0x83 | MATCH_FOUND | `u32 match_id`, `u32 seed`, `u8 seat`, `name16 opp_name`, `u8 opp_kind` |
| 0x84 | ROUND_START | `u8 round`, `i8 hull_you`, `i8 hull_opp`, `u8 energy_you`, `u8 energy_opp`, `i8 hand[4]` (−1 = empty), `u8 opp_hand_size`, `u16 deadline_ms` (0 in fast-forward) |
| 0x85 | PLAY_ACK | `u32 match_id`, `u8 round` |
| 0x86 | PLAY_REJECT | `u32 match_id`, `u8 round`, `u8 reason` (1=illegal card, 2=bad slot, 3=wrong round, 4=already locked, 5=no match) |
| 0x87 | ROUND_RESULT | `u8 round`, `i8 card_you`, `i8 card_opp` (−1 = pass), `u8 dmg_to_you`, `u8 dmg_to_opp`, `i8 hull_you`, `i8 hull_opp` |
| 0x88 | MATCH_END | `u32 match_id`, `u8 result` (0=loss, 1=win, 2=draw), `u8 reason` (0=hull, 1=rounds, 2=forfeit, 3=server) |
| 0x89 | PONG | `u32 nonce` |
| 0x8F | ERROR | `u8 code` (1=bad proto, 2=auth, 3=bad frame, 4=bad state), then connection closes |

## Session state machine

`CONNECTED --HELLO--> (auth required: --AUTH--> VERIFYING, ERROR 2 on rejection) READY --QUEUE--> QUEUED --MATCH_FOUND--> IN_MATCH --MATCH_END--> READY`.
Inside a match the server loops `ROUND_START` → collect locks → `ROUND_RESULT`, up to round 8, then `MATCH_END`.
`ROUND_START` for round *n*+1 follows `ROUND_RESULT` *n* immediately. Both clients get every message; opponent
hand contents are never sent.

## Determinism / training

`seed` (from MATCH_FOUND) fully determines both players' shuffles, so a match is replayable from
`(seed, sequence of both players' PLAY slots)`. The training env speaks this protocol as a normal client
(`kind=1`); observation = the fields of ROUND_START/ROUND_RESULT history, action = PLAY slot, mask = legal
slots (`is-legal-play` over the hand) + pass.

## Server matchmaking rule

FIFO queue per `mode`. Two waiting entries pair immediately **unless** both are bots and pairing them would
leave zero bots waiting (the last waiting bot is reserved for a human). Humans always pair ahead of bot–bot.
