"""Pure-Python, in-process server side of docs/WIRE_PROTOCOL.md for tests (no C binary needed).
Every queued client is matched against a scripted random-legal opponent (seat 1 - the client is seat 0).
Not authoritative for the real game: it exists so env/trainer tests run anywhere. Hand dealing here is its own
seeded shuffle, not bit-identical to dw_server's."""
import random
import socket
import struct
import threading

import dw_rules as R
import dw_wire as W


class FakeServer:
    def __init__(self, port=0, seed=1):
        self.ls = socket.socket(); self.ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.ls.bind(("127.0.0.1", port)); self.ls.listen(8)
        self.port = self.ls.getsockname()[1]
        self.seed = seed; self.matches = 0; self._stop = False; self.results = []
        threading.Thread(target=self._accept, daemon=True).start()

    def close(self):
        self._stop = True
        try: self.ls.close()
        except OSError: pass

    def _accept(self):
        while not self._stop:
            try: c, _ = self.ls.accept()
            except OSError: return
            c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            threading.Thread(target=self._serve, args=(c,), daemon=True).start()

    @staticmethod
    def _rd(c, n):
        b = b""
        while len(b) < n:
            x = c.recv(n - len(b))
            if not x: raise ConnectionError
            b += x
        return b

    def _frame(self, c):
        (ln,) = struct.unpack("<H", self._rd(c, 2)); b = self._rd(c, ln); return b[0], b[1:]

    def _serve(self, c):
        try:
            t, _ = self._frame(c)
            assert t == W.C_HELLO
            c.sendall(W.frame(W.S_WELCOME, struct.pack("<IB", 1, 1)))
            while True:
                t, p = self._frame(c)
                if t == W.C_QUEUE: self._match(c)
                elif t == W.C_AUTH: pass  # fake server: no auth required; ignore like --no-auth
                elif t == W.C_PING: c.sendall(W.frame(W.S_PONG, p[:4]))
                elif t == W.C_LEAVE: return
        except (ConnectionError, OSError, AssertionError):
            pass
        finally:
            c.close()

    def _match(self, c):
        self.matches += 1; mid = self.matches; seed = self.seed + mid
        rng = random.Random(seed)
        piles = [rng.sample(range(9), 9) for _ in range(2)]
        hands = [[piles[s].pop() for _ in range(4)] for s in range(2)]
        hull, energy = [R.START_HULL] * 2, [R.START_ENERGY] * 2
        c.sendall(W.frame(W.S_MATCH_FOUND, struct.pack("<IIB", mid, seed, 0) + W.name16("scripted") + bytes([1])))
        reason, rnd = 1, 0
        while rnd < R.MAX_ROUNDS:
            rnd += 1
            energy = [R.round_start_energy(e) for e in energy]
            c.sendall(W.frame(W.S_ROUND_START, struct.pack("<BbbBB", rnd, hull[0], hull[1], energy[0], energy[1])
                              + struct.pack("<4b", *hands[0]) + bytes([4]) + struct.pack("<H", 0)
                              + bytes([0, 0]) + struct.pack("<bb", R.START_VAULT + rnd, R.START_VAULT + rnd) + bytes([0, 0, 0])))
            while True:
                t, p = self._frame(c)
                if t != W.C_PLAY: continue
                m, r, slot = struct.unpack("<IBb", p[:6])
                if r != rnd or m != mid: c.sendall(W.frame(W.S_PLAY_REJECT, struct.pack("<IBB", m, r, 3))); continue
                if slot != -1 and not (0 <= slot < 4 and R.is_legal_play(hands[0][slot], energy[0], R.START_VAULT + rnd)):
                    c.sendall(W.frame(W.S_PLAY_REJECT, struct.pack("<IBB", m, r, 1 if 0 <= slot < 4 else 2))); continue
                break
            c.sendall(W.frame(W.S_PLAY_ACK, struct.pack("<IB", mid, rnd)))
            cards = [hands[0][slot] if slot >= 0 else -1, -1]
            legal1 = [i for i, cc in enumerate(hands[1]) if R.is_legal_play(cc, energy[1], R.START_VAULT + rnd)]
            s1 = rng.choice(legal1 + [-1]); cards[1] = hands[1][s1] if s1 >= 0 else -1
            slots = [slot, s1]
            dmg = [R.damage_dealt(cards[0], cards[1]), R.damage_dealt(cards[1], cards[0])]  # dmg[s] dealt BY seat s
            hull = [hull[0] - dmg[1], hull[1] - dmg[0]]
            for s in range(2):
                energy[s] = R.energy_after_play(energy[s], cards[s])
                if slots[s] >= 0:
                    if not piles[s]: piles[s] = rng.sample(range(9), 9)
                    hands[s][slots[s]] = piles[s].pop()
            c.sendall(W.frame(W.S_ROUND_RESULT, struct.pack("<BbbBBbb", rnd, cards[0], cards[1], dmg[1], dmg[0], hull[0], hull[1])
                              + struct.pack("<bb", cards[0], cards[1]) + bytes([0, 0]) + struct.pack("<bb", R.START_VAULT + rnd, R.START_VAULT + rnd)
                              + bytes([0, 0, 0, 0, 0, 0])))
            if R.match_decided(*hull): reason = 0; break
        res = 2 if hull[0] == hull[1] else (1 if hull[0] > hull[1] else 0)
        self.results.append(res)
        c.sendall(W.frame(W.S_MATCH_END, struct.pack("<IBB", mid, res, reason)))
