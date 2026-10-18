#!/usr/bin/env python3
"""Tiny stand-in for IDUNA's DEADWEIGHT contract (docs/IDUNA_CONTRACT.md) so CI can exercise dw_server's real HTTP
auth + match-result path without the real IDUNA. Tokens: 'guest.<name>.x' -> human, 'botagent.x.y' -> bot.
Also serves what the constructed queue needs: the public player profile (rating), an in-memory Elo like IDUNA's applyGameMatch that
is answered in the match-result response (seat0/seat1), and GET /bot-decks (the tracker's strength tiers) with deterministic legal decks.
Usage: fake_iduna.py PORTFILE RESULTS_FILE [RATINGS_JSON]   (binds a free port, writes it to PORTFILE, appends match-result bodies
to RESULTS_FILE as JSON lines, appends every bot-decks request to RESULTS_FILE.decks; RATINGS_JSON maps display names to starting
ratings, everyone else starts at 1500; exits on SIGTERM)."""
import json, signal, sys, uuid
from http.server import BaseHTTPRequestHandler, HTTPServer

results_path = sys.argv[2]
seen = set()
start_ratings = json.load(open(sys.argv[3])) if len(sys.argv) > 3 else {}
ratings = {}        # player_id -> rating (float), the in-memory ladder
played = {}         # player_id -> matches
names = {}          # player_id -> display_name
ELO_K = 32          # IDUNA's gameEloK

def pid_for(name):
    return str(uuid.uuid5(uuid.NAMESPACE_DNS, name))

def seen_player(pid, name):
    names[pid] = name
    ratings.setdefault(pid, float(start_ratings.get(name, 1500)))
    played.setdefault(pid, 100 if name.startswith("bot-") else 0)   # bot-* are established; anything else (fresh-*, humans) has no history

def deck_for(tier, k):
    """A deterministic legal deck (ten 1-ofs, five 2-ofs, one 3-of) for bot-decks tier `tier`, variant k."""
    base = (tier * 11 + k * 17) % 100
    ids = [(base + i * 3) % 105 for i in range(16)]
    assert len(set(ids)) == 16
    return ids[:10] + [c for c in ids[10:15] for _ in (0, 1)] + [ids[15]] * 3

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        path = self.path.split("?")[0]
        q = dict(kv.split("=") for kv in self.path.split("?", 1)[1].split("&")) if "?" in self.path else {}
        parts = path.strip("/").split("/")
        if parts[:5] == ["api", "v1", "games", "deadweight", "players"] and len(parts) == 7 and parts[6] == "profile":
            pid = parts[5]
            if pid not in ratings: return self._send(404, {"error": "no such player"})
            return self._send(200, {"player_id": pid, "display_name": names[pid], "rating": ratings[pid], "wins": 0, "losses": 0, "draws": 0,
                                    "matches": played[pid], "friend_count": 0})
        if path == "/api/v1/games/deadweight/bot-decks":
            tier, n = int(q.get("tier", 0)), int(q.get("n", 24))
            with open(results_path + ".decks", "a") as f: f.write(json.dumps({"tier": tier, "tiers": int(q.get("tiers", 5)), "n": n}) + "\n")
            if tier < 1: return self._send(400, {"error": "tier must be 1..tiers"})
            decks = [{"deck_id": 1000 * tier + k, "player": "bot-wall-d", "kind": 1, "created": 1, "cards": deck_for(tier, k), "games": 10, "wins": 9, "losses": 1, "draws": 0, "win_rate": 0.9, "score": 0.5}
                     for k in range(min(n, 3))]
            return self._send(200, {"tier": tier, "tiers": int(q.get("tiers", 5)), "min_games": 4, "eligible": 100, "from": 0, "to": 1, "score_min": 0.1, "score_max": 0.9, "decks": decks})
        self._send(404, {"error": "not found"})
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = json.loads(self.rfile.read(n) or b"{}")
        auth = self.headers.get("Authorization", "")
        tok = auth[7:] if auth.startswith("Bearer ") else ""
        if self.path == "/api/v1/auth/agent":
            return self._send(200, {"access_token": "srvagent.x.y" if body.get("agent_name") == "DEADWEIGHT-SERVER" else "botagent.x.y", "expires_in": 3600, "success": True, "token_type": "Bearer"})
        if self.path == "/api/v1/games/deadweight/verify":
            if tok.startswith("guest."):
                name = tok.split(".")[1]
                seen_player(pid_for("h" + name), name)
                return self._send(200, {"player_id": pid_for("h" + name), "display_name": name, "kind": "human", "game": "deadweight"})
            if tok == "botagent.x.y" and body.get("name"):
                seen_player(pid_for("b" + body["name"]), body["name"])
                return self._send(200, {"player_id": pid_for("b" + body["name"]), "display_name": body["name"], "kind": "bot", "game": "deadweight"})
            return self._send(401, {"error": "invalid token"})
        if self.path == "/api/v1/games/deadweight/match-result":
            if tok != "srvagent.x.y": return self._send(401, {"error": "unauthorized"})
            key = (body["match_id"], body["seed"], body["seat0_player_id"], body["seat1_player_id"])
            dup = key in seen; seen.add(key)
            a, b = body["seat0_player_id"], body["seat1_player_id"]
            for pid in (a, b): ratings.setdefault(pid, 1500.0); played.setdefault(pid, 0); names.setdefault(pid, pid)
            if not dup:
                with open(results_path, "a") as f: f.write(json.dumps(body) + "\n")
                score_a = {0: 1.0, 1: 0.0, 2: 0.5}[body["winner"]]
                exp_a = 1.0 / (1.0 + 10 ** ((ratings[b] - ratings[a]) / 400.0))
                ratings[a] += ELO_K * (score_a - exp_a)
                ratings[b] += ELO_K * ((1 - score_a) - (1 - exp_a))
                played[a] += 1; played[b] += 1
            return self._send(200, {"ok": True, "duplicate": dup, "seat0": {"rating": ratings[a], "matches": played[a]}, "seat1": {"rating": ratings[b], "matches": played[b]}})
        self._send(404, {"error": "not found"})

srv = HTTPServer(("127.0.0.1", 0), H)
open(sys.argv[1], "w").write(str(srv.server_address[1]))
signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))
srv.serve_forever()
