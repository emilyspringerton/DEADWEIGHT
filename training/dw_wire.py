"""Frame codec for docs/WIRE_PROTOCOL.md v1 (TCP, little-endian, u16 len + u8 type + payload)."""
import socket
import struct

C_HELLO, C_QUEUE, C_PLAY, C_LEAVE, C_PING = 0x01, 0x02, 0x03, 0x04, 0x05
S_WELCOME, S_QUEUED, S_MATCH_FOUND, S_ROUND_START, S_PLAY_ACK = 0x81, 0x82, 0x83, 0x84, 0x85
S_PLAY_REJECT, S_ROUND_RESULT, S_MATCH_END, S_PONG, S_ERROR = 0x86, 0x87, 0x88, 0x89, 0x8F
MODE_CARD, KIND_HUMAN, KIND_BOT = 0, 0, 1


def name16(s):
    return s.encode()[:16].ljust(16, b"\0")


def frame(ftype, payload=b""):
    return struct.pack("<HB", len(payload) + 1, ftype) + payload


def hello(name, kind=KIND_BOT, mode=MODE_CARD, token=b""):
    return frame(C_HELLO, struct.pack("<BBB", 1, mode, kind) + name16(name) + bytes([len(token)]) + token)


def queue(): return frame(C_QUEUE)
def play(match_id, rnd, slot): return frame(C_PLAY, struct.pack("<IBb", match_id, rnd, slot))


def parse(ftype, p):
    """Decode a server->client payload into a dict."""
    if ftype == S_WELCOME:
        sid, fl = struct.unpack("<IB", p[:5]); return {"session_id": sid, "flags": fl}
    if ftype == S_QUEUED: return {"waiting": struct.unpack("<H", p[:2])[0]}
    if ftype == S_MATCH_FOUND:
        mid, seed, seat = struct.unpack("<IIB", p[:9])
        return {"match_id": mid, "seed": seed, "seat": seat, "opp_name": p[9:25].rstrip(b"\0").decode(errors="replace"), "opp_kind": p[25]}
    if ftype == S_ROUND_START:
        r, hy, ho, ey, eo = struct.unpack("<BbbBB", p[:5])
        return {"round": r, "hull_you": hy, "hull_opp": ho, "energy_you": ey, "energy_opp": eo,
                "hand": list(struct.unpack("<4b", p[5:9])), "opp_hand_size": p[9], "deadline_ms": struct.unpack("<H", p[10:12])[0]}
    if ftype == S_PLAY_ACK: return {"match_id": struct.unpack("<I", p[:4])[0], "round": p[4]}
    if ftype == S_PLAY_REJECT: return {"match_id": struct.unpack("<I", p[:4])[0], "round": p[4], "reason": p[5]}
    if ftype == S_ROUND_RESULT:
        r, cy, co, dy, do, hy, ho = struct.unpack("<BbbBBbb", p[:7])
        return {"round": r, "card_you": cy, "card_opp": co, "dmg_to_you": dy, "dmg_to_opp": do, "hull_you": hy, "hull_opp": ho}
    if ftype == S_MATCH_END:
        mid, res, why = struct.unpack("<IBB", p[:6]); return {"match_id": mid, "result": res, "reason": why}
    if ftype == S_PONG: return {"nonce": struct.unpack("<I", p[:4])[0]}
    if ftype == S_ERROR: return {"code": p[0]}
    return {}


class Conn:
    def __init__(self, host, port, timeout=30.0):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def send(self, data): self.s.sendall(data)

    def _read(self, n):
        buf = b""
        while len(buf) < n:
            c = self.s.recv(n - len(buf))
            if not c: raise ConnectionError("server closed connection")
            buf += c
        return buf

    def recv(self):
        (ln,) = struct.unpack("<H", self._read(2))
        body = self._read(ln)
        return body[0], parse(body[0], body[1:])

    def close(self):
        try: self.s.close()
        except OSError: pass
