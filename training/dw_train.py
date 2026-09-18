#!/usr/bin/env python3
"""DEADWEIGHT league trainer (S503-08). Shape ported from BRAWLPIT/scripts/rl_train_packet.py, simplified for a
5-action card game: THREE models (MAIN / MAIN_EXPLOITER / LEAGUE_EXPLOITER, rl_league.py) train one after another
against opponents sampled from the league by the ported PFSP logic; every --save-freq cycles all three checkpoints
are saved and registered together (register_generation_snapshot), roles inherit Elo (Main Exploiter resets every
--reset-main-exploiter-every generations), and are optionally pushed to the IDUNA game-scoped registry.

Servers: each role gets its OWN `dw_server --fast-forward --port N` (same binary as the live pool, separate ports and
league dir). A trainee (kind=bot) needs an opponent, so per role a background OpponentBot (kind=human, so the server's
"never pair the last waiting bot" rule can't strand the pair) queues on the same server and plays the sampled league
member's policy (or random-legal for the heuristic baseline / dry-run).

HONEST STATUS: sb3-contrib/gymnasium are not installed in the sandbox this was written in, so real MaskablePPO
training has NOT been run (BRAWLPIT "flagged not faked" precedent) -- code targets their documented APIs behind
guarded imports. `--dry-run` exercises league + registry + env + Elo wiring end to end with a random policy against
the in-process fake server (or the real server with --server-bin).

  python3 dw_train.py --dry-run --generations 2 --episodes 10
  python3 dw_train.py --server-bin ../build/dw_server --generations 50 --timesteps 4096 [--registry-url URL]
"""
import argparse
import json
import os
import random
import signal
import subprocess
import sys
import threading
import time

import dw_env
import dw_wire as W
from rl_league import (ALL_ROLES, register_generation_snapshot, HEURISTIC_ID, LeagueManager, LeagueRole, sample_for_league_exploiter,
                       sample_for_main, sample_for_main_exploiter, should_reset_main_exploiter,
                       DEFAULT_STRUGGLE_WINDOW)
import rl_registry

try:  # optional heavy deps
    from sb3_contrib import MaskablePPO
except ImportError:
    MaskablePPO = None

DEFAULT_ENT_COEF = 0.01  # BRAWLPIT commit 055beb8: SB3's default 0 collapsed the policy; always explicit.
_procs = []


def spawn_server(binary, port):
    p = subprocess.Popen([binary, "--fast-forward", "--no-auth", "--port", str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _procs.append(p)
    time.sleep(0.3)
    if p.poll() is not None:
        raise RuntimeError(f"{binary} exited immediately (rc={p.returncode}); is the server implemented yet?")
    return p


def cleanup_servers():
    for p in _procs:  # exact PIDs we spawned only -- never pkill by name
        if p.poll() is None:
            p.terminate()
    for p in _procs:
        try: p.wait(timeout=5)
        except subprocess.TimeoutExpired: p.kill()
    _procs.clear()


def load_policy(path):
    """Returns policy(obs, mask)->action for a saved checkpoint; random-legal when unavailable."""
    if path and MaskablePPO is not None and os.path.exists(path) and not path.endswith(".json"):
        import numpy as np
        model = MaskablePPO.load(path, device="cpu")
        return lambda obs, mask: int(model.predict(np.asarray(obs, dtype=np.float32), action_masks=np.asarray(mask), deterministic=False)[0])
    return lambda obs, mask: dw_env.random_policy(mask)


class OpponentBot(threading.Thread):
    """Queues on the role's server and plays `policy` until stop()."""

    def __init__(self, host, port, policy, name="league-opp"):
        super().__init__(daemon=True)
        self.env = dw_env.DwEnv(host, port, name=name, kind=W.KIND_HUMAN)
        self.policy, self._stop_flag = policy, False

    def run(self):
        try:
            while not self._stop_flag:
                obs, _ = self.env.reset(); done = False
                while not done and not self._stop_flag:
                    obs, _, done, _, _ = self.env.step(self.policy(obs, self.env.action_masks()))
        except (OSError, ConnectionError, RuntimeError, AttributeError):
            pass

    def stop(self):
        self._stop_flag = True
        try:
            if self.env.conn: self.env.conn.send(W.frame(W.C_LEAVE))
        except OSError:
            pass
        self.env.close()
        self.join(timeout=2)
        time.sleep(0.25)  # let the server drop our queue entry, or the next generation's opponent pairs with this ghost


def pick_opponent(role, league, local_wl, recent_vs_main, rng=None):
    """Adapter over the ported rl_league sampling -> (checkpoint_path|None, member_id|None)."""
    paths = {m.id: m.path for m in league.all_members()}
    if role == LeagueRole.MAIN_EXPLOITER:
        pid = sample_for_main_exploiter(league, local_wl, recent_vs_main, rng=rng)
    elif role == LeagueRole.LEAGUE_EXPLOITER:
        pid = sample_for_league_exploiter(league, local_wl, rng=rng)
    else:
        pid = sample_for_main(league, local_wl, rng=rng)
    if pid is None or pid == HEURISTIC_ID or pid not in paths:
        return None, None
    return paths[pid], pid


def run_role(role, host, port, model, opp_policy, steps_or_episodes, dry):
    """Trains (or, dry, just plays) one role for a budget; returns list of match results (1 win/0 loss/2 draw)."""
    opp = OpponentBot(host, port, opp_policy) if opp_policy is not None else None
    if opp: opp.start()
    env = dw_env.DwEnv(host, port, name=f"train-{role.value}"[:16], kind=W.KIND_BOT)
    results = []
    try:
        if dry:
            results = dw_env.play_episodes(env, steps_or_episodes)
        else:
            from stable_baselines3.common.callbacks import BaseCallback

            class Rec(BaseCallback):
                def _on_step(self):
                    for i, d in enumerate(self.locals.get("dones", [])):
                        if d: results.append(self.locals["infos"][i].get("match_result", 2))
                    return True

            model.set_env(env)
            model.learn(total_timesteps=steps_or_episodes, callback=Rec(), reset_num_timesteps=False)
    finally:
        env.close()
        if opp: opp.stop()
    return results


def fresh_model(env, ent_coef):
    return MaskablePPO("MlpPolicy", env, ent_coef=ent_coef, verbose=0, device="cpu")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true", help="random policy; no sb3/gymnasium needed")
    ap.add_argument("--server-bin", help="path to dw_server; omit with --dry-run to use the in-process fake server")
    ap.add_argument("--host", default="127.0.0.1"); ap.add_argument("--base-port", type=int, default=7100)
    ap.add_argument("--league-dir", default="league_data")
    ap.add_argument("--generations", type=int, default=3)
    ap.add_argument("--save-freq", type=int, default=1, help="register a 3-role snapshot every N generations")
    ap.add_argument("--timesteps", type=int, default=4096, help="PPO timesteps per role per generation")
    ap.add_argument("--episodes", type=int, default=20, help="dry-run matches per role per generation")
    ap.add_argument("--ent-coef", type=float, default=DEFAULT_ENT_COEF)
    ap.add_argument("--reset-main-exploiter-every", type=int, default=5)
    ap.add_argument("--registry-url", help="IDUNA base URL; omit for local-only")
    ap.add_argument("--agent-secret", default=os.environ.get("IDUNA_AGENT_SECRET"))
    ap.add_argument("--source-location", default="local")
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args(argv)

    if not a.dry_run and MaskablePPO is None:
        raise SystemExit("sb3-contrib/gymnasium not installed: pip install sb3-contrib gymnasium (or use --dry-run)")
    rng = random.Random(a.seed)
    league = LeagueManager(a.league_dir)
    registry = rl_registry.make_registry(a.registry_url, a.agent_secret, os.path.join(a.league_dir, "registry_local"))
    ckpt_dir = os.path.join(a.league_dir, "checkpoints"); os.makedirs(ckpt_dir, exist_ok=True)

    fake = []
    ports = {}
    for i, role in enumerate(ALL_ROLES):
        if a.server_bin:
            ports[role] = a.base_port + i; spawn_server(a.server_bin, ports[role])
        else:
            from fake_server import FakeServer
            fs = FakeServer(seed=a.seed + i); fake.append(fs); ports[role] = fs.port
    signal.signal(signal.SIGTERM, lambda *_: (cleanup_servers(), sys.exit(1)))

    models = {r: None for r in ALL_ROLES}
    local_wl = {r: {} for r in ALL_ROLES}
    recent_vs_main = []
    start_gen = 1 + max([m.generation for m in league.all_members()] or [0])
    try:
        for gen in range(start_gen, start_gen + a.generations):
            reset_roles = set()
            for role in ALL_ROLES:
                if role == LeagueRole.MAIN_EXPLOITER and should_reset_main_exploiter(gen, a.reset_main_exploiter_every):
                    models[role] = None; reset_roles.add(role)
                opp_path, opp_id = pick_opponent(role, league, local_wl[role], recent_vs_main, rng)
                opp_policy = load_policy(opp_path) if a.server_bin else None  # fake server supplies its own opponent
                if not a.dry_run and models[role] is None:
                    models[role] = fresh_model(dw_env.DwEnv(a.host, ports[role]), a.ent_coef)
                budget = a.episodes if a.dry_run else a.timesteps
                results = run_role(role, a.host, ports[role], models[role], opp_policy, budget, a.dry_run)
                me = league.latest_by_role(role)
                if opp_id:
                    w = sum(1 for r in results if r == 1); l = sum(1 for r in results if r == 0)
                    ow, ol = local_wl[role].get(opp_id, (0, 0)); local_wl[role][opp_id] = (ow + w, ol + l)
                    if me is not None:
                        for r in results: league.record_match_result(me.id, opp_id, {1: 1.0, 0: 0.0}.get(r, 0.5))
                    if role == LeagueRole.MAIN_EXPLOITER and opp_id == (league.latest_by_role(LeagueRole.MAIN) or me).id:
                        recent_vs_main = (recent_vs_main + [1 if r == 1 else 0 for r in results])[-DEFAULT_STRUGGLE_WINDOW:]
                wins = sum(1 for r in results if r == 1)
                print(f"gen {gen} {role.value:17s} vs {opp_id or 'heuristic':>28s}: {wins}/{len(results)} wins", flush=True)
            if (gen - start_gen + 1) % a.save_freq == 0:
                paths = {}
                for role in ALL_ROLES:
                    p = os.path.join(ckpt_dir, f"{role.value}_g{gen}." + ("json" if a.dry_run else "zip"))
                    if a.dry_run:
                        with open(p, "w") as f: json.dump({"dry_run": True, "role": role.value, "generation": gen}, f)
                    else:
                        models[role].save(p)
                    paths[role] = p
                members = register_generation_snapshot(league, gen, paths, reset_roles=reset_roles)
                for role, m in members.items():
                    registry.push(role.value, gen, league.get_elo(m.id), a.source_location, paths[role])
                print(f"gen {gen}: registered snapshot ({', '.join(f'{r.value}={league.get_elo(m.id):.0f}' for r, m in members.items())})", flush=True)
    finally:
        cleanup_servers()
        for fs in fake: fs.close()
    return league


if __name__ == "__main__":
    main()
