#!/usr/bin/env python3
"""
training/rl_registry.py (S503-08; adapted from BRAWLPIT/scripts/rl_registry.py, S420) -- a real, minimal client for IDUNA's own remote RL checkpoint
registry (POST/GET {REG}, IDUNA/internal/brawlpit/checkpoint_store.go).
Founder real-time: "lets make a checkpoint registry so we can train from multiple locations and
then we can add checkpoints from colab?"

scripts/rl_league.py's own LeagueManager is a real, working registry, but it's a LOCAL filesystem
directory shared only by processes on the SAME machine. This module is the network half: any
training location (this box, a Colab runtime, a future machine) authenticates as the real
DEADWEIGHT-RL M2M agent and pushes/pulls checkpoints through IDUNA -- one real, shared, always-
reachable source of truth instead of N independent local league_data/ directories that never see
each other.

Uses only the standard library (urllib) -- no `requests` dependency, matching the same "keep the
training pipeline's own dependency footprint minimal" discipline scripts/rl_env_packet.py already
follows (it needed no HTTP library at all, just raw sockets).
"""

import json
import mimetypes
import os
import urllib.error
import urllib.parse
import urllib.request
import uuid

GAME = "deadweight"
# Game-scoped registry (docs/IDUNA_CONTRACT.md): same routes/format as brawlpit-checkpoints, different base path.
REG = f"/api/v1/game-checkpoints/{GAME}"


def authenticate(base_url, agent_name, agent_secret):
    """POST /api/v1/auth/agent -- returns a real, short-lived (1hr) Bearer JWT. Same real M2M
    flow every other agent in this monorepo (REDGARDEN-BOTS, ECOWAR-BOTS, ...) already uses."""
    body = json.dumps({"agent_name": agent_name, "agent_secret": agent_secret}).encode()
    req = urllib.request.Request(
        f"{base_url}/api/v1/auth/agent", data=body,
        headers={"Content-Type": "application/json"}, method="POST",
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        data = json.load(resp)
    return data["access_token"]


def _multipart_body(fields, files):
    """Real, minimal multipart/form-data encoder -- the standard library has no built-in one
    (urllib only handles application/x-www-form-urlencoded natively), and pulling in `requests`
    just for this one call would be a heavier dependency than the rest of this pipeline needs.

    `files` is a list of (field_name, filename, bytes) tuples -- S421-02 needs two real file
    fields in one request (`file`, the .zip; `weights_file`, the exported native-inference
    blob), not just one."""
    boundary = uuid.uuid4().hex
    parts = []
    for name, value in fields.items():
        parts.append(f"--{boundary}\r\n".encode())
        parts.append(f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode())
        parts.append(f"{value}\r\n".encode())
    for field_name, filename, file_bytes in files:
        content_type = mimetypes.guess_type(filename)[0] or "application/octet-stream"
        parts.append(f"--{boundary}\r\n".encode())
        parts.append(
            f'Content-Disposition: form-data; name="{field_name}"; filename="{filename}"\r\n'
            f"Content-Type: {content_type}\r\n\r\n".encode()
        )
        parts.append(file_bytes)
        parts.append(b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())
    return b"".join(parts), f"multipart/form-data; boundary={boundary}"


def push_checkpoint(base_url, jwt, role, generation, elo, source_location, path, weights_path=None):
    """POST {REG} -- uploads one real checkpoint file + its metadata, and,
    if `weights_path` is given, the real exported native-inference weights blob
    (scripts/export_policy_weights.py's own "BPMW" format) alongside it in the SAME request
    (S421-02, founder real-time: "ensure that the client actually uses that model"). Requires a
    JWT carrying the real brawlpit.checkpoints.write permission (see IDUNA/migrations/truestore/
    202609131400_brawlpit_rl_checkpoints.sql's own new DEADWEIGHT-RL agent). Returns the real
    registered Checkpoint dict (id/name/sha256/size_bytes/has_weights/created_at)."""
    with open(path, "rb") as f:
        file_bytes = f.read()
    files = [("file", os.path.basename(path), file_bytes)]
    if weights_path:
        with open(weights_path, "rb") as f:
            weights_bytes = f.read()
        files.append(("weights_file", os.path.basename(weights_path), weights_bytes))

    body, content_type = _multipart_body(
        {"role": role, "generation": generation, "elo": elo, "source_location": source_location},
        files,
    )
    req = urllib.request.Request(
        f"{base_url}{REG}", data=body,
        headers={"Content-Type": content_type, "Authorization": f"Bearer {jwt}"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"push_checkpoint failed ({e.code}): {e.read().decode(errors='replace')}") from e


def record_match_result(base_url, jwt, a_id, b_id, score_a):
    """POST {REG}/match-result -- the ONLY thing that ever moves a
    checkpoint's Elo off its inherited value (see IDUNA/internal/brawlpit/checkpoint_store.go's
    own RecordMatchResult doc comment). `score_a` is 1.0 (A won), 0.0 (A lost), or 0.5 (a real
    draw) -- IDUNA applies the standard Elo formula to both sides in one transaction. Requires a
    JWT carrying the real brawlpit.checkpoints.write permission, same as push_checkpoint. Returns
    the real {"a": Checkpoint, "b": Checkpoint} dict with both sides' updated Elo."""
    body = json.dumps({"a_id": a_id, "b_id": b_id, "score_a": score_a}).encode()
    req = urllib.request.Request(
        f"{base_url}{REG}/match-result", data=body,
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {jwt}"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"record_match_result failed ({e.code}): {e.read().decode(errors='replace')}") from e


def list_checkpoints(base_url, role=None):
    """GET {REG}[?role=...] -- real, public, no auth needed (same trust
    level GET /api/v1/brawlpit-levels already established)."""
    url = f"{base_url}{REG}"
    if role:
        url += f"?role={urllib.parse.quote(role)}"
    with urllib.request.urlopen(url, timeout=15) as resp:
        return json.load(resp)


def download_checkpoint(base_url, checkpoint_id, dest_path):
    """GET {REG}/<id>/download -- real, public, streams the raw .zip
    bytes to `dest_path`. Returns dest_path for convenience."""
    url = f"{base_url}{REG}/{checkpoint_id}/download"
    with urllib.request.urlopen(url, timeout=60) as resp:
        data = resp.read()
    with open(dest_path, "wb") as f:
        f.write(data)
    return dest_path


class LocalRegistry:
    """No-IDUNA fallback: same push/list surface, backed by a local directory (copies + an ndjson index). Lets training
    run anywhere with zero remote dependencies; checkpoints can be pushed to IDUNA later with `push`."""

    def __init__(self, root):
        self.root = root
        os.makedirs(root, exist_ok=True)
        self.index = os.path.join(root, "index.ndjson")

    def push(self, role, generation, elo, source_location, path, weights_path=None):
        import shutil
        dest = os.path.join(self.root, f"{role}_g{generation}_{os.path.basename(path)}")
        shutil.copyfile(path, dest)
        rec = {"id": sum(1 for _ in open(self.index)) + 1 if os.path.exists(self.index) else 1, "role": role,
               "generation": generation, "elo": elo, "source_location": source_location, "path": dest}
        with open(self.index, "a") as f:
            f.write(json.dumps(rec) + "\n")
        return rec

    def list(self, role=None):
        if not os.path.exists(self.index):
            return []
        with open(self.index) as f:
            recs = [json.loads(l) for l in f if l.strip()]
        return [r for r in recs if role is None or r["role"] == role]


class RemoteRegistry:
    """IDUNA-backed registry (game-scoped). Authenticates lazily; re-auths on 401 is left to the caller (1h JWT)."""

    def __init__(self, base_url, agent_name="DEADWEIGHT-RL", agent_secret=None):
        self.base_url, self.agent_name, self.agent_secret, self._jwt = base_url, agent_name, agent_secret, None

    def _token(self):
        if self._jwt is None:
            if not self.agent_secret:
                raise RuntimeError("agent secret required (IDUNA_AGENT_SECRET) for registry writes")
            self._jwt = authenticate(self.base_url, self.agent_name, self.agent_secret)
        return self._jwt

    def push(self, role, generation, elo, source_location, path, weights_path=None):
        return push_checkpoint(self.base_url, self._token(), role, generation, elo, source_location, path, weights_path)

    def list(self, role=None):
        return list_checkpoints(self.base_url, role)


def make_registry(base_url=None, agent_secret=None, local_dir=None):
    """Remote when base_url is given, else local directory (default ./registry_local)."""
    if base_url:
        return RemoteRegistry(base_url, agent_secret=agent_secret)
    return LocalRegistry(local_dir or "registry_local")


if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    push_p = sub.add_parser("push", help="upload one checkpoint to the remote registry")
    push_p.add_argument("--base-url", default=os.environ.get("IDUNA_BASE_URL", "http://localhost:8080"))
    push_p.add_argument("--agent-name", default=os.environ.get("IDUNA_AGENT_NAME", "DEADWEIGHT-RL"))
    push_p.add_argument("--agent-secret", default=os.environ.get("IDUNA_AGENT_SECRET"))
    push_p.add_argument("--role", required=True)
    push_p.add_argument("--generation", type=int, required=True)
    push_p.add_argument("--elo", type=float, required=True)
    push_p.add_argument("--source-location", required=True)
    push_p.add_argument("--weights", help="optional exported weights .bin (scripts/export_policy_weights.py)")
    push_p.add_argument("path")

    list_p = sub.add_parser("list", help="list checkpoints in the remote registry")
    list_p.add_argument("--base-url", default=os.environ.get("IDUNA_BASE_URL", "http://localhost:8080"))
    list_p.add_argument("--role")

    pull_p = sub.add_parser("pull", help="download one checkpoint by id")
    pull_p.add_argument("--base-url", default=os.environ.get("IDUNA_BASE_URL", "http://localhost:8080"))
    pull_p.add_argument("id", type=int)
    pull_p.add_argument("dest")

    args = p.parse_args()

    if args.cmd == "push":
        if not args.agent_secret:
            raise SystemExit("--agent-secret (or IDUNA_AGENT_SECRET) is required")
        jwt = authenticate(args.base_url, args.agent_name, args.agent_secret)
        result = push_checkpoint(args.base_url, jwt, args.role, args.generation, args.elo,
                                  args.source_location, args.path, weights_path=args.weights)
        print(json.dumps(result, indent=2))
    elif args.cmd == "list":
        for c in list_checkpoints(args.base_url, args.role):
            print(f"id={c['id']:4d}  name={c.get('name', ''):28s}  role={c['role']:18s}  gen={c['generation']:3d}  "
                  f"elo={c['elo']:7.1f}  weights={'yes' if c.get('has_weights') else 'no ':3s}  from={c['source_location']}")
    elif args.cmd == "pull":
        path = download_checkpoint(args.base_url, args.id, args.dest)
        print(f"downloaded -> {path}")
