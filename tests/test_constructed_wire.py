#!/usr/bin/env python3
"""Raw-protocol checks of DW_MODE_CONSTRUCTED against a running dw_server (docs/WIRE_PROTOCOL.md): the server must validate a submitted
deck itself (never trust the client's), refuse a QUEUE with no deck, answer a good deck with DRAFT_DONE, and keep the queues apart.
Usage: test_constructed_wire.py PORT"""
import socket, struct, sys

port = int(sys.argv[1])
HELLO, QUEUE, DRAFT_RESUME = 0x01, 0x02, 0x08
WELCOME, QUEUED, DRAFT_DONE, ERROR = 0x81, 0x82, 0x8B, 0x8F

def frame(t, payload=b""):
    return struct.pack("<HB", len(payload) + 1, t) + payload

def hello(mode, name):
    return frame(HELLO, bytes([3, mode, 0]) + name.encode().ljust(16, b"\0") + b"\0")

def read_frame(s, timeout=3.0):
    s.settimeout(timeout)
    buf = b""
    while len(buf) < 2:
        d = s.recv(2 - len(buf))
        if not d: return None
        buf += d
    n = struct.unpack("<H", buf)[0]
    body = b""
    while len(body) < n:
        d = s.recv(n - len(body))
        if not d: return None
        body += d
    return body[0], body[1:]

def connect(mode, name):
    s = socket.create_connection(("127.0.0.1", port), timeout=3)
    s.sendall(hello(mode, name))
    f = read_frame(s)
    assert f and f[0] == WELCOME, f"no WELCOME for {name}: {f}"
    return s

def legal_deck(offset=0):
    ids = [(offset + i * 5) % 105 for i in range(16)]
    assert len(set(ids)) == 16
    return bytes(ids[:10] + [c for c in ids[10:15] for _ in (0, 1)] + [ids[15]] * 3)

fails = 0
def check(cond, msg):
    global fails
    print(("PASS: " if cond else "FAIL: ") + msg)
    if not cond: fails += 1

# 1. a legal deck is accepted (DRAFT_DONE echoes it back with an id), and only then can the player queue
s = connect(3, "wire-ok")
s.sendall(frame(DRAFT_RESUME, legal_deck(3)))
f = read_frame(s)
check(f and f[0] == DRAFT_DONE and f[1][4:] == legal_deck(3), "a legal built deck is accepted: DRAFT_DONE echoes the 23 cards")
s.sendall(frame(QUEUE, b"\x01"))
f = read_frame(s)
check(f and f[0] == QUEUED, "QUEUE with the deck enters the constructed queue")
s.close()

# 2. QUEUE with no deck is refused
s = connect(3, "wire-nodeck")
s.sendall(frame(QUEUE))
f = read_frame(s)
check(f and f[0] == ERROR and f[1][0] == 4, "QUEUE before any deck is ERROR 4 (bad state)")
s.close()

# 3. every kind of illegal deck is refused by the server itself
bad = {
    "23 copies of one card": bytes([0]) * 23,
    "wrong bucket shape (11 singles, 4 pairs, 1 triple)": bytes(list(range(11)) + [c for c in range(11, 15) for _ in (0, 1)] + [15] * 3),
    "four copies": bytes(list(range(9)) + [9] * 4 + [c for c in range(10, 14) for _ in (0, 1)] + [14] * 2 + [15] * 2)[:23],
    "a card id outside the catalog": legal_deck(0)[:22] + bytes([120]),
    "negative id": legal_deck(0)[:22] + bytes([0xFF]),
}
for why, deck in bad.items():
    s = connect(3, "wire-bad")
    s.sendall(frame(DRAFT_RESUME, deck))
    f = read_frame(s)
    check(f and f[0] == ERROR and f[1][0] == 3, f"illegal deck refused with ERROR 3 ({why})")
    s.close()

# 4. a random-queue client cannot submit a deck (that is for draft and constructed)
s = connect(0, "wire-random")
s.sendall(frame(DRAFT_RESUME, legal_deck(1)))
f = read_frame(s)
check(f and f[0] == ERROR and f[1][0] == 4, "a random-queue client sending a deck is ERROR 4")
s.close()

# 5. a deck can be replaced while READY (the player went back to the builder) and the last one wins
s = connect(3, "wire-replace")
s.sendall(frame(DRAFT_RESUME, legal_deck(1)))
read_frame(s)
s.sendall(frame(DRAFT_RESUME, legal_deck(7)))
f = read_frame(s)
check(f and f[0] == DRAFT_DONE and f[1][4:] == legal_deck(7), "a second deck replaces the first while READY")
s.close()

# 6. a mode the server does not have is refused at HELLO
s = socket.create_connection(("127.0.0.1", port), timeout=3)
s.sendall(hello(9, "wire-mode9"))
f = read_frame(s)
check(f and f[0] == ERROR, "an unknown HELLO mode is refused")
s.close()

print(f"constructed wire: {'FAILED' if fails else 'ok'} ({fails} failures)")
sys.exit(1 if fails else 0)
