// wasmProto.ts -- a drop-in, wasm-backed replacement for proto.ts's encode*/decodeServerFrame
// functions. Same public function names, same exported types (re-exported from proto.ts
// directly, since the field shapes are identical) -- client.ts/main.ts can switch their import
// from './proto' to './wasmProto' with zero other changes.
//
// The actual encoding/decoding happens in apps/wasm/protocol_wasm.c + core/protocol.c, compiled
// to a real, native (non-Emscripten) wasm32 module by scripts/build_wasm_native.sh -- see
// docs/NATIVE_WASM_CLIENT_NORTHSTAR.md. This file is a thin JS/TS adapter around that module's
// exports: it owns fetching + instantiating the .wasm, and translates between this module's
// plain-object ServerFrame shapes and the wasm module's named getter calls. It never re-derives
// wire-format knowledge itself -- FrameDecoder (framing only, no protocol semantics) is reused
// from proto.ts verbatim rather than duplicated.
import { FrameDecoder, PROTO_VERSION, ClientMsg, ServerMsg } from './proto.js';
import type {
    ServerFrame, Welcome, Queued, MatchFound, RoundStart, PlayAck, PlayReject, RoundResult,
    MatchEnd, Pong, ErrorMsg, DraftOffer, DraftDone,
} from './proto.js';

export { FrameDecoder, PROTO_VERSION, ClientMsg, ServerMsg };
export type {
    ServerFrame, Welcome, Queued, MatchFound, RoundStart, PlayAck, PlayReject, RoundResult,
    MatchEnd, Pong, ErrorMsg, DraftOffer, DraftDone,
};

interface WasmExports {
    memory: WebAssembly.Memory;
    encode_buf_ptr(): number;
    decode_in_ptr(): number;
    wasm_encode(): number;
    wasm_decode(len: number): number;
    get_msg_type(): number;
    set_hello(proto: number, mode: number, kind: number, tokenLen: number): void;
    set_hello_name_char(i: number, c: number): void;
    set_hello_token_byte(i: number, b: number): void;
    set_auth(tokenLen: number): void;
    set_auth_token_byte(i: number, b: number): void;
    set_queue(sameDeck: number, hasMatchToken: number): void;
    set_queue_token_char(i: number, c: number): void;
    set_play(matchId: number, round: number, slot: number): void;
    set_leave(): void;
    set_ping(nonce: number): void;
    get_welcome_session_id(): number; get_welcome_flags(): number;
    get_queued_waiting(): number;
    get_match_found_match_id(): number; get_match_found_seed(): number; get_match_found_seat(): number;
    get_match_found_opp_kind(): number; get_match_found_opp_name_char(i: number): number;
    get_round_start_round(): number; get_round_start_hull_you(): number; get_round_start_hull_opp(): number;
    get_round_start_energy_you(): number; get_round_start_energy_opp(): number; get_round_start_hand(i: number): number;
    get_round_start_opp_hand_size(): number; get_round_start_deadline_ms(): number;
    get_round_start_armor_you(): number; get_round_start_armor_opp(): number;
    get_round_start_vault_you(): number; get_round_start_vault_opp(): number;
    get_round_start_lock_mask(): number; get_round_start_status_you(): number; get_round_start_status_opp(): number;
    get_play_ack_match_id(): number; get_play_ack_round(): number;
    get_play_reject_match_id(): number; get_play_reject_round(): number; get_play_reject_reason(): number;
    get_round_result_round(): number; get_round_result_card_you(): number; get_round_result_card_opp(): number;
    get_round_result_dmg_you(): number; get_round_result_dmg_opp(): number;
    get_round_result_hull_you(): number; get_round_result_hull_opp(): number;
    get_round_result_eff_you(): number; get_round_result_eff_opp(): number;
    get_round_result_armor_you(): number; get_round_result_armor_opp(): number;
    get_round_result_vault_you(): number; get_round_result_vault_opp(): number;
    get_round_result_heal_you(): number; get_round_result_heal_opp(): number;
    get_round_result_roll_you(): number; get_round_result_roll_opp(): number;
    get_round_result_flags_you(): number; get_round_result_flags_opp(): number;
    get_match_end_match_id(): number; get_match_end_result(): number; get_match_end_reason(): number;
    get_pong_nonce(): number;
    get_draft_offer_pick_no(): number; get_draft_offer_total(): number;
    get_draft_offer_card(i: number): number; get_draft_offer_left(i: number): number;
    get_draft_done_deck_id(): number; get_draft_done_card(i: number): number;
    get_error_code(): number;
}

let wasm: WasmExports | null = null;

/** Fetches + instantiates dw_protocol.wasm. Must be awaited once before any encode/decode call
 * below. wasmUrl defaults to where scripts/build_wasm_native.sh writes it, relative to
 * index.html (dist/generated/dw_protocol.wasm, matching cards.json's own sibling location). */
export async function initWasmProto(wasmUrl = 'dist/generated/dw_protocol.wasm'): Promise<void> {
    const bytes = await (await fetch(wasmUrl)).arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes, {});
    wasm = instance.exports as unknown as WasmExports;
}

function requireWasm(): WasmExports {
    if (!wasm) throw new Error('wasmProto: initWasmProto() must be awaited before use');
    return wasm;
}

function readEncoded(w: WasmExports, n: number): Uint8Array {
    const mem = new Uint8Array(w.memory.buffer);
    // Slice (copy), not subarray: the underlying ArrayBuffer can be detached/resized by a later
    // wasm call (memory.grow), so the caller must not hold a view into it.
    return mem.slice(w.encode_buf_ptr(), w.encode_buf_ptr() + n);
}

function writeName(w: WasmExports, name: string, setChar: (i: number, c: number) => void, len: number) {
    const enc = new TextEncoder().encode(name).slice(0, len);
    for (let i = 0; i < len; i++) setChar.call(w, i, i < enc.length ? enc[i] : 0);
}

export function encodeHello(mode: 0 | 1 | 2, kind: 0 | 1, name: string, token: string): Uint8Array {
    const w = requireWasm();
    const tokenBytes = new TextEncoder().encode(token).slice(0, 200);
    w.set_hello(PROTO_VERSION, mode, kind, tokenBytes.length);
    writeName(w, name, w.set_hello_name_char, 17);
    tokenBytes.forEach((b, i) => w.set_hello_token_byte(i, b));
    return readEncoded(w, w.wasm_encode());
}

/** AUTH (0x06): the real IDUNA player JWT, up to 900 bytes (docs/WIRE_PROTOCOL.md's AUTH row --
 * "Real IDUNA ES256 JWTs are ~400-500 bytes, far above HELLO's 200-byte token field"). Sent right
 * after HELLO (which then carries an empty inline token) whenever a real token exists; a
 * --no-auth server just ignores it. */
export function encodeAuth(token: string): Uint8Array {
    const w = requireWasm();
    const tokenBytes = new TextEncoder().encode(token).slice(0, 900);
    w.set_auth(tokenBytes.length);
    tokenBytes.forEach((b, i) => w.set_auth_token_byte(i, b));
    return readEncoded(w, w.wasm_encode());
}

export function encodeQueue(sameDeck?: 0 | 1, matchToken?: string): Uint8Array {
    const w = requireWasm();
    if (matchToken) {
        w.set_queue(sameDeck ?? 0, 1);
        const tokenBytes = new TextEncoder().encode(matchToken).slice(0, 32);
        for (let i = 0; i < 33; i++) w.set_queue_token_char(i, i < tokenBytes.length ? tokenBytes[i] : 0);
    } else {
        // matches proto.ts's own encodeQueue: with no matchToken and sameDeck===undefined the
        // wire payload is 0 bytes -- set_queue's own fields are irrelevant then, but dw_encode's
        // C switch reads m->u.queue.has_match_token to decide payload length, so it must be 0.
        w.set_queue(sameDeck ?? 0, 0);
    }
    return readEncoded(w, w.wasm_encode());
}

export function encodePlay(matchId: number, round: number, slot: number): Uint8Array {
    const w = requireWasm();
    w.set_play(matchId, round, slot);
    return readEncoded(w, w.wasm_encode());
}

export function encodeLeave(): Uint8Array {
    const w = requireWasm();
    w.set_leave();
    return readEncoded(w, w.wasm_encode());
}

export function encodePing(nonce: number): Uint8Array {
    const w = requireWasm();
    w.set_ping(nonce);
    return readEncoded(w, w.wasm_encode());
}

function readName(w: WasmExports, getChar: (i: number) => number, len: number): string {
    let out = '';
    for (let i = 0; i < len; i++) {
        const c = getChar.call(w, i);
        if (c === 0) break;
        out += String.fromCharCode(c);
    }
    return out;
}

/** Decodes one already-length-delimited frame's type+payload bytes -- same input contract as
 * proto.ts's decodeServerFrame (FrameDecoder hands it typeAndPayload with the 2-byte length
 * header already stripped). The wasm module's wasm_decode expects the FULL wire frame (length
 * header included, matching core/protocol.c's dw_decode signature), so this re-adds it. */
export function decodeServerFrame(typeAndPayload: Uint8Array): ServerFrame {
    const w = requireWasm();
    const frame = new Uint8Array(2 + typeAndPayload.length);
    frame[0] = typeAndPayload.length & 0xff;
    frame[1] = (typeAndPayload.length >> 8) & 0xff;
    frame.set(typeAndPayload, 2);
    const mem = () => new Uint8Array(w.memory.buffer);
    mem().set(frame, w.decode_in_ptr());
    const r = w.wasm_decode(frame.length);
    const msgType = w.get_msg_type();
    if (r !== 1) return { type: 'UNKNOWN', msgType };

    switch (msgType) {
        case ServerMsg.WELCOME:
            return { type: 'WELCOME', sessionId: w.get_welcome_session_id(), fastForward: !!(w.get_welcome_flags() & 1), authRequired: !!(w.get_welcome_flags() & 2) };
        case ServerMsg.QUEUED:
            return { type: 'QUEUED', waiting: w.get_queued_waiting() };
        case ServerMsg.MATCH_FOUND:
            return {
                type: 'MATCH_FOUND', matchId: w.get_match_found_match_id(), seed: w.get_match_found_seed(),
                seat: w.get_match_found_seat(), oppName: readName(w, w.get_match_found_opp_name_char, 16),
                oppKind: w.get_match_found_opp_kind(),
            };
        case ServerMsg.ROUND_START:
            return {
                type: 'ROUND_START', round: w.get_round_start_round(), hullYou: w.get_round_start_hull_you(),
                hullOpp: w.get_round_start_hull_opp(), energyYou: w.get_round_start_energy_you(),
                energyOpp: w.get_round_start_energy_opp(),
                hand: [w.get_round_start_hand(0), w.get_round_start_hand(1), w.get_round_start_hand(2), w.get_round_start_hand(3)],
                oppHandSize: w.get_round_start_opp_hand_size(), deadlineMs: w.get_round_start_deadline_ms(),
                armorYou: w.get_round_start_armor_you(), armorOpp: w.get_round_start_armor_opp(),
                vaultYou: w.get_round_start_vault_you(), vaultOpp: w.get_round_start_vault_opp(),
                lockMask: w.get_round_start_lock_mask(), statusYou: w.get_round_start_status_you(),
                statusOpp: w.get_round_start_status_opp(),
            };
        case ServerMsg.PLAY_ACK:
            return { type: 'PLAY_ACK', matchId: w.get_play_ack_match_id(), round: w.get_play_ack_round() };
        case ServerMsg.PLAY_REJECT:
            return { type: 'PLAY_REJECT', matchId: w.get_play_reject_match_id(), round: w.get_play_reject_round(), reason: w.get_play_reject_reason() };
        case ServerMsg.ROUND_RESULT:
            return {
                type: 'ROUND_RESULT', round: w.get_round_result_round(), cardYou: w.get_round_result_card_you(),
                cardOpp: w.get_round_result_card_opp(), dmgToYou: w.get_round_result_dmg_you(), dmgToOpp: w.get_round_result_dmg_opp(),
                hullYou: w.get_round_result_hull_you(), hullOpp: w.get_round_result_hull_opp(),
                effYou: w.get_round_result_eff_you(), effOpp: w.get_round_result_eff_opp(),
                armorYou: w.get_round_result_armor_you(), armorOpp: w.get_round_result_armor_opp(),
                vaultYou: w.get_round_result_vault_you(), vaultOpp: w.get_round_result_vault_opp(),
                healYou: w.get_round_result_heal_you(), healOpp: w.get_round_result_heal_opp(),
                rollYou: w.get_round_result_roll_you(), rollOpp: w.get_round_result_roll_opp(),
                flagsYou: w.get_round_result_flags_you(), flagsOpp: w.get_round_result_flags_opp(),
            };
        case ServerMsg.MATCH_END:
            return { type: 'MATCH_END', matchId: w.get_match_end_match_id(), result: w.get_match_end_result(), reason: w.get_match_end_reason() };
        case ServerMsg.PONG:
            return { type: 'PONG', nonce: w.get_pong_nonce() };
        case ServerMsg.DRAFT_OFFER:
            return {
                type: 'DRAFT_OFFER', pickNo: w.get_draft_offer_pick_no(), total: w.get_draft_offer_total(),
                card: [w.get_draft_offer_card(0), w.get_draft_offer_card(1)],
                left: [w.get_draft_offer_left(0), w.get_draft_offer_left(1), w.get_draft_offer_left(2)],
            };
        case ServerMsg.DRAFT_DONE: {
            const cards: number[] = [];
            for (let i = 0; i < 23; i++) cards.push(w.get_draft_done_card(i));
            return { type: 'DRAFT_DONE', deckId: w.get_draft_done_deck_id(), cards };
        }
        case ServerMsg.ERROR:
            return { type: 'ERROR', code: w.get_error_code() };
        default:
            return { type: 'UNKNOWN', msgType };
    }
}
