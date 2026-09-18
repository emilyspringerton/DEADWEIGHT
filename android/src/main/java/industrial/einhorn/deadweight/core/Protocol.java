package industrial.einhorn.deadweight.core;

import java.nio.charset.StandardCharsets;

/** Wire protocol v1 codec (docs/WIRE_PROTOCOL.md). Little-endian; frame = u16 len + u8 type + payload. */
public final class Protocol {
    private Protocol() {}

    public static final int PROTO = 1;
    public static final int MAX_FRAME = 256;

    public static final int C_HELLO = 0x01, C_QUEUE = 0x02, C_PLAY = 0x03, C_LEAVE = 0x04, C_PING = 0x05;
    public static final int S_WELCOME = 0x81, S_QUEUED = 0x82, S_MATCH_FOUND = 0x83, S_ROUND_START = 0x84,
        S_PLAY_ACK = 0x85, S_PLAY_REJECT = 0x86, S_ROUND_RESULT = 0x87, S_MATCH_END = 0x88, S_PONG = 0x89,
        S_ERROR = 0x8F;

    public static final int MODE_CARD = 0;
    public static final int KIND_HUMAN = 0, KIND_BOT = 1;
    public static final int RESULT_LOSS = 0, RESULT_WIN = 1, RESULT_DRAW = 2;
    public static final int MAX_TOKEN = 200;

    // ---- encoding ----
    private static byte[] frame(int type, int payloadLen) {
        int len = 1 + payloadLen;
        byte[] f = new byte[2 + len];
        f[0] = (byte) len;
        f[1] = (byte) (len >> 8);
        f[2] = (byte) type;
        return f;
    }

    private static void u32(byte[] b, int off, long v) {
        b[off] = (byte) v; b[off + 1] = (byte) (v >> 8); b[off + 2] = (byte) (v >> 16); b[off + 3] = (byte) (v >> 24);
    }

    public static byte[] hello(int mode, int kind, String name, byte[] token) {
        if (token == null) token = new byte[0];
        if (token.length > MAX_TOKEN) throw new IllegalArgumentException("token too long");
        byte[] f = frame(C_HELLO, 3 + 16 + 1 + token.length);
        f[3] = PROTO; f[4] = (byte) mode; f[5] = (byte) kind;
        byte[] nm = name == null ? new byte[0] : name.getBytes(StandardCharsets.UTF_8);
        System.arraycopy(nm, 0, f, 6, Math.min(nm.length, 15)); // always NUL-terminated
        f[22] = (byte) token.length;
        System.arraycopy(token, 0, f, 23, token.length);
        return f;
    }

    public static byte[] queue() { return frame(C_QUEUE, 0); }
    public static byte[] leave() { return frame(C_LEAVE, 0); }

    public static byte[] play(long matchId, int round, int slot) {
        byte[] f = frame(C_PLAY, 6);
        u32(f, 3, matchId); f[7] = (byte) round; f[8] = (byte) slot;
        return f;
    }

    public static byte[] ping(long nonce) {
        byte[] f = frame(C_PING, 4);
        u32(f, 3, nonce);
        return f;
    }

    // ---- decoding (server messages; payload = type byte + body, i.e. frame without the u16 length) ----
    private static final class R {
        final byte[] b; int p = 1; // skip type
        R(byte[] b) { this.b = b; }
        int u8() throws ProtocolException { need(1); return b[p++] & 0xFF; }
        int i8() throws ProtocolException { need(1); return b[p++]; }
        int u16() throws ProtocolException { need(2); int v = (b[p] & 0xFF) | ((b[p + 1] & 0xFF) << 8); p += 2; return v; }
        long u32() throws ProtocolException {
            need(4);
            long v = (b[p] & 0xFFL) | ((b[p + 1] & 0xFFL) << 8) | ((b[p + 2] & 0xFFL) << 16) | ((b[p + 3] & 0xFFL) << 24);
            p += 4; return v;
        }
        String name16() throws ProtocolException {
            need(16);
            int n = 0; while (n < 16 && b[p + n] != 0) n++;
            String s = new String(b, p, n, StandardCharsets.UTF_8); p += 16; return s;
        }
        void need(int n) throws ProtocolException { if (p + n > b.length) throw new ProtocolException("truncated message 0x" + Integer.toHexString(b[0] & 0xFF)); }
        void end() throws ProtocolException { if (p != b.length) throw new ProtocolException("trailing bytes in message 0x" + Integer.toHexString(b[0] & 0xFF)); }
    }

    public static Msg decode(byte[] payload) throws ProtocolException {
        if (payload == null || payload.length < 1) throw new ProtocolException("empty frame");
        Msg m = new Msg();
        m.type = payload[0] & 0xFF;
        R r = new R(payload);
        switch (m.type) {
            case S_WELCOME: m.sessionId = r.u32(); m.flags = r.u8(); break;
            case S_QUEUED: m.waiting = r.u16(); break;
            case S_MATCH_FOUND: m.matchId = r.u32(); m.seed = r.u32(); m.seat = r.u8(); m.oppName = r.name16(); m.oppKind = r.u8(); break;
            case S_ROUND_START:
                m.round = r.u8(); m.hullYou = r.i8(); m.hullOpp = r.i8(); m.energyYou = r.u8(); m.energyOpp = r.u8();
                for (int i = 0; i < 4; i++) m.hand[i] = r.i8();
                m.oppHandSize = r.u8(); m.deadlineMs = r.u16(); break;
            case S_PLAY_ACK: m.matchId = r.u32(); m.round = r.u8(); break;
            case S_PLAY_REJECT: m.matchId = r.u32(); m.round = r.u8(); m.reason = r.u8(); break;
            case S_ROUND_RESULT:
                m.round = r.u8(); m.cardYou = r.i8(); m.cardOpp = r.i8(); m.dmgYou = r.u8(); m.dmgOpp = r.u8();
                m.hullYou = r.i8(); m.hullOpp = r.i8(); break;
            case S_MATCH_END: m.matchId = r.u32(); m.result = r.u8(); m.reason = r.u8(); break;
            case S_PONG: m.nonce = r.u32(); break;
            case S_ERROR: m.code = r.u8(); break;
            default: throw new ProtocolException("unknown message type 0x" + Integer.toHexString(m.type));
        }
        r.end();
        return m;
    }

    // ---- server-side encoders: used only by tests / fake servers ----
    public static byte[] welcome(long sid, int flags) { byte[] f = frame(S_WELCOME, 5); u32(f, 3, sid); f[7] = (byte) flags; return f; }
    public static byte[] queued(int waiting) { byte[] f = frame(S_QUEUED, 2); f[3] = (byte) waiting; f[4] = (byte) (waiting >> 8); return f; }
    public static byte[] matchFound(long mid, long seed, int seat, String opp, int oppKind) {
        byte[] f = frame(S_MATCH_FOUND, 4 + 4 + 1 + 16 + 1);
        u32(f, 3, mid); u32(f, 7, seed); f[11] = (byte) seat;
        byte[] nm = opp.getBytes(StandardCharsets.UTF_8);
        System.arraycopy(nm, 0, f, 12, Math.min(nm.length, 15)); f[28] = (byte) oppKind; return f;
    }
    public static byte[] roundStart(int round, int hy, int ho, int ey, int eo, int[] hand, int oppHand, int deadline) {
        byte[] f = frame(S_ROUND_START, 5 + 4 + 1 + 2);
        f[3] = (byte) round; f[4] = (byte) hy; f[5] = (byte) ho; f[6] = (byte) ey; f[7] = (byte) eo;
        for (int i = 0; i < 4; i++) f[8 + i] = (byte) hand[i];
        f[12] = (byte) oppHand; f[13] = (byte) deadline; f[14] = (byte) (deadline >> 8); return f;
    }
    public static byte[] playAck(long mid, int round) { byte[] f = frame(S_PLAY_ACK, 5); u32(f, 3, mid); f[7] = (byte) round; return f; }
    public static byte[] playReject(long mid, int round, int reason) { byte[] f = frame(S_PLAY_REJECT, 6); u32(f, 3, mid); f[7] = (byte) round; f[8] = (byte) reason; return f; }
    public static byte[] roundResult(int round, int cy, int co, int dy, int dop, int hy, int ho) {
        byte[] f = frame(S_ROUND_RESULT, 7);
        f[3] = (byte) round; f[4] = (byte) cy; f[5] = (byte) co; f[6] = (byte) dy; f[7] = (byte) dop; f[8] = (byte) hy; f[9] = (byte) ho; return f;
    }
    public static byte[] matchEnd(long mid, int result, int reason) { byte[] f = frame(S_MATCH_END, 6); u32(f, 3, mid); f[7] = (byte) result; f[8] = (byte) reason; return f; }
    public static byte[] error(int code) { byte[] f = frame(S_ERROR, 1); f[3] = (byte) code; return f; }
}
