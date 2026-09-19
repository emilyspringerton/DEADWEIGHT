# DEADWEIGHT wire protocol v2 (TCP)

Little-endian. Frame = `u16 len` (bytes after this field) + `u8 type` + payload. Max `len` 1024 (only AUTH is ever larger than 256); longer =
protocol violation, server closes. Strings are fixed 16-byte NUL-padded UTF-8 (`name16`).
One TCP connection = one session. Server sends nothing before HELLO. `proto` = 2 (v2 = the 73-card Guild ruleset, S503-16). A HELLO with any other `proto` gets `ERROR 1 (bad proto)` and a close, so a v1 client fails with a clear message instead of mis-rendering the new meters. v1 → v2 only *appends* fields to ROUND_START / ROUND_RESULT and adds bankruptcy as a match-end reason.

## Client → server

| type | name | payload |
|---|---|---|
| 0x01 | HELLO | `u8 proto`, `u8 mode` (0=card/**random**, 1=backpack reserved, **2=draft**), `u8 kind` (0=human, 1=bot), `name16 name`, `u8 token_len`, `token[token_len ≤ 200]` (IDUNA player token; empty allowed only with `--no-auth`) |
| 0x02 | QUEUE | — or one optional byte `u8 same_deck`. In **draft** mode: no byte / `0` = draft a new deck first (server replies `DRAFT_OFFER`), `1` = requeue with the last drafted deck (falls back to a redraft if there is none). Random mode ignores the byte. |
| 0x03 | PLAY | `u32 match_id`, `u8 round`, `i8 slot` (0–3, −1 = pass) |
| 0x04 | LEAVE | — (leaves queue or forfeits the current match) |
| 0x05 | PING | `u32 nonce` |
| 0x07 | DRAFT_PICK | `u8 index` (0/1: which offered card), `u8 mult` (1–3 copies). Draft mode only, while drafting. An invalid pick (bad index, or that copy-count bucket is full) is not an error: the server just re-sends the current `DRAFT_OFFER`. |
| 0x06 | AUTH | `u16 token_len` (≤ 900), `token[token_len]` — the IDUNA token (guest player JWT, or DEADWEIGHT-BOTS agent JWT for bots). **Real IDUNA ES256 JWTs are ~400–500 bytes, far above HELLO's 200-byte `token` field**, so with auth required the client sends HELLO with `token_len = 0` and then AUTH; the server replies WELCOME only after IDUNA verification. HELLO's inline token (≤200) is still accepted for short tokens. With `--no-auth` the server sends WELCOME straight after HELLO and ignores a later AUTH. |

## Server → client

| type | name | payload |
|---|---|---|
| 0x81 | WELCOME | `u32 session_id`, `u8 server_flags` (bit0 = fast_forward, bit1 = auth_required) |
| 0x82 | QUEUED | `u16 waiting` |
| 0x83 | MATCH_FOUND | `u32 match_id`, `u32 seed`, `u8 seat`, `name16 opp_name`, `u8 opp_kind` |
| 0x84 | ROUND_START | `u8 round`, `i8 hull_you`, `i8 hull_opp`, `u8 energy_you`, `u8 energy_opp`, `i8 hand[4]` (−1 = empty), `u8 opp_hand_size`, `u16 deadline_ms` (0 in fast-forward), **v2:** `u8 armor_you`, `u8 armor_opp`, `i8 vault_you`, `i8 vault_opp` (credits), `u8 lock_mask` (your hand slots you cannot play this round), `u8 status_you`, `u8 status_opp` (bit0 burning, bit1 regenerating, bit2 hidden). While the opponent is hidden by Merkle Blindness, `energy_opp` and `armor_opp` are `255` and `vault_opp` is `-128`. |
| 0x85 | PLAY_ACK | `u32 match_id`, `u8 round` |
| 0x86 | PLAY_REJECT | `u32 match_id`, `u8 round`, `u8 reason` (1=illegal card, 2=bad slot, 3=wrong round, 4=already locked, 5=no match) |
| 0x87 | ROUND_RESULT | `u8 round`, `i8 card_you`, `i8 card_opp` (declared card, −1 = pass), `u8 dmg_to_you`, `u8 dmg_to_opp` (total hull lost this round from every source), `i8 hull_you`, `i8 hull_opp`, **v2:** `i8 eff_you`, `i8 eff_opp` (the card that actually resolved — Dark Pool's result / a copied card; −1 = pass or cancelled), `u8 armor_you`, `u8 armor_opp`, `i8 vault_you`, `i8 vault_opp` (after the round; hidden sentinels as above), `u8 heal_you`, `u8 heal_opp`, `u8 roll_you`, `u8 roll_opp` (this round's 0–99 rolls), `u8 flags_you`, `u8 flags_opp` (bit0 cancelled, bit1 immune, bit2 lifeline saved, bit3 locked opp slots, bit4 hands swapped, bit5 made opp discard, bit6 redrew hand, bit7 copied) |
| 0x88 | MATCH_END | `u32 match_id`, `u8 result` (0=loss, 1=win, 2=draw), `u8 reason` (0=hull, 1=rounds, 2=forfeit, 3=server, **4=bankrupt**) |
| 0x89 | PONG | `u32 nonce` |
| 0x8A | DRAFT_OFFER | `u8 pick_no` (0–15), `u8 total` (16), `i8 card[2]` (the two offered cards), `u8 left[3]` (copy-count buckets remaining: 1-ofs, 2-ofs, 3-ofs; a bucket at 0 is greyed out). Sent after `QUEUE` in draft mode and after every accepted pick; 16 picks total. |
| 0x8B | DRAFT_DONE | `u32 deck_id` (server-assigned, unique per drafted deck, the key into `decks.ndjson`), `i8 cards[23]` (the finished deck in pick order). Sent instead of a 17th offer; the server then sends `QUEUED` (draft queue). |
| 0x8F | ERROR | `u8 code` (1=bad proto, 2=auth, 3=bad frame, 4=bad state), then connection closes |

## Session state machine

`CONNECTED --HELLO--> (auth required: --AUTH--> VERIFYING, ERROR 2 on rejection) READY --QUEUE--> QUEUED --MATCH_FOUND--> IN_MATCH --MATCH_END--> READY`.
Draft mode inserts a draft: `READY --QUEUE--> DRAFTING (DRAFT_OFFER / DRAFT_PICK x16) --DRAFT_DONE--> QUEUED --MATCH_FOUND--> ...`; after `MATCH_END`, `QUEUE(same_deck=1)` skips the draft and `QUEUE` (or `same_deck=0`) redrafts. `LEAVE` while drafting returns to READY.
Inside a match the server loops `ROUND_START` → collect locks → `ROUND_RESULT`, up to round 100, then `MATCH_END`.
`ROUND_START` for round *n*+1 follows `ROUND_RESULT` *n* immediately. Both clients get every message; opponent
hand contents are never sent.

## Determinism / training

`seed` (from MATCH_FOUND) fully determines both players' shuffles, every per-round roll and every random target (locks, discards), so a match is replayable from
`(seed, sequence of both players' PLAY slots)`. The training env speaks this protocol as a normal client
(`kind=1`); observation = the fields of ROUND_START/ROUND_RESULT history, action = PLAY slot, mask = legal
slots (`is-legal-play(card, energy, credits)` over the hand, minus `lock_mask`) + pass.

## Server matchmaking rule

FIFO queue per `mode` (**random and draft are fully separate queues**: a draft player only ever meets someone in the draft queue, so each queue needs its own bot pool of 3). Two waiting entries pair immediately **unless** both are bots and pairing them would
leave zero bots waiting (the last waiting bot is reserved for a human). Humans always pair ahead of bot–bot.
