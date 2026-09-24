// DEADWEIGHT wire protocol v3 codec — see docs/WIRE_PROTOCOL.md (that doc is the source of truth;
// this file is a hand-written, direct TypeScript port of its byte layout, not generated).
//
// Little-endian. Frame = u16 len (bytes after this field) + u8 type + payload. Strings are fixed
// 16-byte NUL-padded UTF-8 ("name16"). This module only encodes/decodes bytes; it knows nothing
// about game rules (those come from generated/CardRules.ts) or transport (WebSocket, handled by
// client.ts).

export const PROTO_VERSION = 3;

export const ClientMsg = {
    HELLO: 0x01,
    QUEUE: 0x02,
    PLAY: 0x03,
    LEAVE: 0x04,
    PING: 0x05,
    AUTH: 0x06,
    DRAFT_PICK: 0x07,
} as const;

export const ServerMsg = {
    WELCOME: 0x81,
    QUEUED: 0x82,
    MATCH_FOUND: 0x83,
    ROUND_START: 0x84,
    PLAY_ACK: 0x85,
    PLAY_REJECT: 0x86,
    ROUND_RESULT: 0x87,
    MATCH_END: 0x88,
    PONG: 0x89,
    DRAFT_OFFER: 0x8a,
    DRAFT_DONE: 0x8b,
    ERROR: 0x8f,
} as const;

// --- little-endian binary writer, growable, matching the frame shape above ---
class Writer {
    private bytes: number[] = [];
    u8(v: number) { this.bytes.push(v & 0xff); }
    i8(v: number) { this.bytes.push(v & 0xff); }
    u16(v: number) { this.bytes.push(v & 0xff, (v >> 8) & 0xff); }
    u32(v: number) { this.bytes.push(v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >>> 24) & 0xff); }
    bytesRaw(b: Uint8Array | number[]) { for (const x of b) this.bytes.push(x & 0xff); }
    name16(s: string) {
        const enc = new TextEncoder().encode(s).slice(0, 16);
        const buf = new Uint8Array(16);
        buf.set(enc);
        this.bytesRaw(buf);
    }
    toBytes(): Uint8Array { return new Uint8Array(this.bytes); }
}

// --- little-endian binary reader over one frame's payload ---
class Reader {
    private pos = 0;
    constructor(private view: DataView) {}
    u8(): number { return this.view.getUint8(this.pos++); }
    i8(): number { return this.view.getInt8(this.pos++); }
    u16(): number { const v = this.view.getUint16(this.pos, true); this.pos += 2; return v; }
    u32(): number { const v = this.view.getUint32(this.pos, true); this.pos += 4; return v; }
    name16(): string {
        const bytes = new Uint8Array(this.view.buffer, this.view.byteOffset + this.pos, 16);
        this.pos += 16;
        const nul = bytes.indexOf(0);
        return new TextDecoder().decode(nul === -1 ? bytes : bytes.subarray(0, nul));
    }
    remaining(): number { return this.view.byteLength - this.pos; }
}

/** Wraps a type+payload into a full frame (u16 len + u8 type + payload), ready to send. */
function frame(type: number, payload: Uint8Array): Uint8Array {
    const len = payload.length + 1; // +1 for the type byte, matching "len = bytes after this field"
    const out = new Uint8Array(2 + 1 + payload.length);
    out[0] = len & 0xff;
    out[1] = (len >> 8) & 0xff;
    out[2] = type;
    out.set(payload, 3);
    return out;
}

export function encodeHello(mode: 0 | 1 | 2, kind: 0 | 1, name: string, token: string): Uint8Array {
    const w = new Writer();
    w.u8(PROTO_VERSION);
    w.u8(mode);
    w.u8(kind);
    w.name16(name);
    const tokenBytes = new TextEncoder().encode(token).slice(0, 200);
    w.u8(tokenBytes.length);
    w.bytesRaw(tokenBytes);
    return frame(ClientMsg.HELLO, w.toBytes());
}

/** matchToken (S537 Duel Phase 2): a purely additive, no-proto-bump extension of QUEUE's already-
 * variable payload (0, 1, or 33 bytes -- docs/WIRE_PROTOCOL.md). When present, the wire form is
 * ALWAYS same_deck-byte-then-32-raw-token-bytes (matching core/protocol.c's dw_encode -- same_deck
 * is written even when 0, since the 33-byte length itself is what signals "token present" to the
 * decoder). Encoded to exactly 32 bytes, zero-padded/truncated, matching the C client's fixed
 * char[33] buffer (IDUNA always mints exactly 32 hex chars, so this never actually truncates). */
export function encodeQueue(sameDeck?: 0 | 1, matchToken?: string): Uint8Array {
    const w = new Writer();
    if (matchToken) {
        w.u8(sameDeck ?? 0);
        const tokenBytes = new Uint8Array(32);
        tokenBytes.set(new TextEncoder().encode(matchToken).slice(0, 32));
        w.bytesRaw(tokenBytes);
    } else if (sameDeck !== undefined) {
        w.u8(sameDeck);
    }
    return frame(ClientMsg.QUEUE, w.toBytes());
}

export function encodePlay(matchId: number, round: number, slot: number): Uint8Array {
    const w = new Writer();
    w.u32(matchId);
    w.u8(round);
    w.i8(slot);
    return frame(ClientMsg.PLAY, w.toBytes());
}

export function encodeLeave(): Uint8Array {
    return frame(ClientMsg.LEAVE, new Uint8Array(0));
}

export function encodePing(nonce: number): Uint8Array {
    const w = new Writer();
    w.u32(nonce);
    return frame(ClientMsg.PING, w.toBytes());
}

// --- decoded server message shapes ---
export type Welcome = { type: 'WELCOME'; sessionId: number; fastForward: boolean; authRequired: boolean };
export type Queued = { type: 'QUEUED'; waiting: number };
export type MatchFound = { type: 'MATCH_FOUND'; matchId: number; seed: number; seat: number; oppName: string; oppKind: number };
export type RoundStart = {
    type: 'ROUND_START'; round: number; hullYou: number; hullOpp: number; energyYou: number; energyOpp: number;
    hand: number[]; oppHandSize: number; deadlineMs: number; armorYou: number; armorOpp: number;
    vaultYou: number; vaultOpp: number; lockMask: number; statusYou: number; statusOpp: number;
};
export type PlayAck = { type: 'PLAY_ACK'; matchId: number; round: number };
export type PlayReject = { type: 'PLAY_REJECT'; matchId: number; round: number; reason: number };
export type RoundResult = {
    type: 'ROUND_RESULT'; round: number; cardYou: number; cardOpp: number; dmgToYou: number; dmgToOpp: number;
    hullYou: number; hullOpp: number; effYou: number; effOpp: number; armorYou: number; armorOpp: number;
    vaultYou: number; vaultOpp: number; healYou: number; healOpp: number; rollYou: number; rollOpp: number;
    flagsYou: number; flagsOpp: number;
};
export type MatchEnd = { type: 'MATCH_END'; matchId: number; result: number; reason: number };
export type Pong = { type: 'PONG'; nonce: number };
export type ErrorMsg = { type: 'ERROR'; code: number };
export type DraftOffer = { type: 'DRAFT_OFFER'; pickNo: number; total: number; card: [number, number]; left: [number, number, number] };
export type DraftDone = { type: 'DRAFT_DONE'; deckId: number; cards: number[] };

export type ServerFrame =
    | Welcome | Queued | MatchFound | RoundStart | PlayAck | PlayReject | RoundResult | MatchEnd | Pong
    | ErrorMsg | DraftOffer | DraftDone
    | { type: 'UNKNOWN'; msgType: number };

/** Decodes one already-length-delimited frame's type+payload bytes (see FrameDecoder below for
 * pulling exactly one frame's bytes out of a WebSocket message / TCP stream). */
export function decodeServerFrame(typeAndPayload: Uint8Array): ServerFrame {
    const view = new DataView(typeAndPayload.buffer, typeAndPayload.byteOffset, typeAndPayload.byteLength);
    const msgType = view.getUint8(0);
    const r = new Reader(new DataView(typeAndPayload.buffer, typeAndPayload.byteOffset + 1, typeAndPayload.byteLength - 1));

    switch (msgType) {
        case ServerMsg.WELCOME: {
            const sessionId = r.u32();
            const flags = r.u8();
            return { type: 'WELCOME', sessionId, fastForward: !!(flags & 1), authRequired: !!(flags & 2) };
        }
        case ServerMsg.QUEUED:
            return { type: 'QUEUED', waiting: r.u16() };
        case ServerMsg.MATCH_FOUND: {
            const matchId = r.u32();
            const seed = r.u32();
            const seat = r.u8();
            const oppName = r.name16();
            const oppKind = r.u8();
            return { type: 'MATCH_FOUND', matchId, seed, seat, oppName, oppKind };
        }
        case ServerMsg.ROUND_START: {
            const round = r.u8();
            const hullYou = r.i8();
            const hullOpp = r.i8();
            const energyYou = r.u8();
            const energyOpp = r.u8();
            const hand = [r.i8(), r.i8(), r.i8(), r.i8()];
            const oppHandSize = r.u8();
            const deadlineMs = r.u16();
            const armorYou = r.u8();
            const armorOpp = r.u8();
            const vaultYou = r.i8();
            const vaultOpp = r.i8();
            const lockMask = r.u8();
            const statusYou = r.u8();
            const statusOpp = r.u8();
            return {
                type: 'ROUND_START', round, hullYou, hullOpp, energyYou, energyOpp, hand, oppHandSize,
                deadlineMs, armorYou, armorOpp, vaultYou, vaultOpp, lockMask, statusYou, statusOpp,
            };
        }
        case ServerMsg.PLAY_ACK:
            return { type: 'PLAY_ACK', matchId: r.u32(), round: r.u8() };
        case ServerMsg.PLAY_REJECT:
            return { type: 'PLAY_REJECT', matchId: r.u32(), round: r.u8(), reason: r.u8() };
        case ServerMsg.ROUND_RESULT: {
            const round = r.u8();
            const cardYou = r.i8();
            const cardOpp = r.i8();
            const dmgToYou = r.u8();
            const dmgToOpp = r.u8();
            const hullYou = r.i8();
            const hullOpp = r.i8();
            const effYou = r.i8();
            const effOpp = r.i8();
            const armorYou = r.u8();
            const armorOpp = r.u8();
            const vaultYou = r.i8();
            const vaultOpp = r.i8();
            const healYou = r.u8();
            const healOpp = r.u8();
            const rollYou = r.u8();
            const rollOpp = r.u8();
            const flagsYou = r.u8();
            const flagsOpp = r.u8();
            return {
                type: 'ROUND_RESULT', round, cardYou, cardOpp, dmgToYou, dmgToOpp, hullYou, hullOpp,
                effYou, effOpp, armorYou, armorOpp, vaultYou, vaultOpp, healYou, healOpp, rollYou, rollOpp,
                flagsYou, flagsOpp,
            };
        }
        case ServerMsg.MATCH_END:
            return { type: 'MATCH_END', matchId: r.u32(), result: r.u8(), reason: r.u8() };
        case ServerMsg.PONG:
            return { type: 'PONG', nonce: r.u32() };
        case ServerMsg.DRAFT_OFFER: {
            const pickNo = r.u8();
            const total = r.u8();
            const card: [number, number] = [r.i8(), r.i8()];
            const left: [number, number, number] = [r.u8(), r.u8(), r.u8()];
            return { type: 'DRAFT_OFFER', pickNo, total, card, left };
        }
        case ServerMsg.DRAFT_DONE: {
            const deckId = r.u32();
            const cards: number[] = [];
            for (let i = 0; i < 23; i++) cards.push(r.i8());
            return { type: 'DRAFT_DONE', deckId, cards };
        }
        case ServerMsg.ERROR:
            return { type: 'ERROR', code: r.u8() };
        default:
            return { type: 'UNKNOWN', msgType };
    }
}

/** Feed raw bytes in as they arrive (WebSocket binary messages may split or coalesce frames);
 * pulls out complete frames and calls onFrame(type, payloadWithType) for each. Payload passed to
 * onFrame includes the leading type byte, matching decodeServerFrame's own expected input. */
export class FrameDecoder {
    private buf: Uint8Array = new Uint8Array(0);
    push(chunk: Uint8Array, onFrame: (typeAndPayload: Uint8Array) => void) {
        const merged = new Uint8Array(this.buf.length + chunk.length);
        merged.set(this.buf);
        merged.set(chunk, this.buf.length);
        this.buf = merged;

        while (this.buf.length >= 2) {
            const len = this.buf[0] | (this.buf[1] << 8);
            if (this.buf.length < 2 + len) break; // wait for more bytes
            const typeAndPayload = this.buf.slice(2, 2 + len);
            this.buf = this.buf.slice(2 + len);
            onFrame(typeAndPayload);
        }
    }
}
