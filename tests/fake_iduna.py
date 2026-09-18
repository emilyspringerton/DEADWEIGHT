#!/usr/bin/env python3
"""Tiny stand-in for IDUNA's DEADWEIGHT contract (docs/IDUNA_CONTRACT.md) so CI can exercise dw_server's real HTTP
auth + match-result path without the real IDUNA. Tokens: 'guest.<name>.x' -> human, 'botagent.x.y' -> bot.
Usage: fake_iduna.py PORTFILE RESULTS_FILE   (binds a free port, writes it to PORTFILE, appends match-result bodies
to RESULTS_FILE as JSON lines; exits on SIGTERM)."""
import json, signal, sys, uuid
from http.server import BaseHTTPRequestHandler, HTTPServer

results_path = sys.argv[2]
seen = set()

def pid_for(name):
    return str(uuid.uuid5(uuid.NAMESPACE_DNS, name))

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = json.loads(self.rfile.read(n) or b"{}")
        auth = self.headers.get("Authorization", "")
        tok = auth[7:] if auth.startswith("Bearer ") else ""
        if self.path == "/api/v1/auth/agent":
            return self._send(200, {"access_token": "srvagent.x.y" if body.get("agent_name") == "DEADWEIGHT-SERVER" else "botagent.x.y", "expires_in": 3600, "success": True, "token_type": "Bearer"})
        if self.path == "/api/v1/games/deadweight/verify":
            if tok.startswith("guest."):
                name = tok.split(".")[1]
                return self._send(200, {"player_id": pid_for("h" + name), "display_name": name, "kind": "human", "game": "deadweight"})
            if tok == "botagent.x.y" and body.get("name"):
                return self._send(200, {"player_id": pid_for("b" + body["name"]), "display_name": body["name"], "kind": "bot", "game": "deadweight"})
            return self._send(401, {"error": "invalid token"})
        if self.path == "/api/v1/games/deadweight/match-result":
            if tok != "srvagent.x.y": return self._send(401, {"error": "unauthorized"})
            key = (body["match_id"], body["seed"], body["seat0_player_id"], body["seat1_player_id"])
            dup = key in seen; seen.add(key)
            if not dup:
                with open(results_path, "a") as f: f.write(json.dumps(body) + "\n")
            return self._send(200, {"ok": True, "duplicate": dup})
        self._send(404, {"error": "not found"})

srv = HTTPServer(("127.0.0.1", 0), H)
open(sys.argv[1], "w").write(str(srv.server_address[1]))
signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))
srv.serve_forever()
