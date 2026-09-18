#!/usr/bin/env python3
"""DEADWEIGHT card-mode training env: a real TCP client of docs/WIRE_PROTOCOL.md (bot HELLO, QUEUE, PLAY, parse
ROUND_START / ROUND_RESULT / MATCH_END) -- the packet-level design of BRAWLPIT's rl_env_packet.py. The policy is a
drop-in bot: it sees exactly what a client sees.

OBSERVATION (58 floats, OBS_DIM):
  [0] hull_you/20   [1] hull_opp/20   [2] energy_you/6   [3] energy_opp/6   [4] round/8
  [5..44]  hand: 4 slots x 10 one-hot (card id 0..8, index 9 = empty slot)
  [45] opp_hand_size/4
  [46..57] opponent's last 3 played kinds, oldest first: 3 x 4 one-hot (BURST, TANK, SHIELD, none/pass)
ACTION: Discrete(5): 0..3 = play hand slot, 4 = pass. ACTION MASK (action_masks()) = dw_rules.legal_mask (port of
  PARENA card_rules; parity-checked against tests/parity_vectors.txt). An illegal action sent anyway is converted to
  pass locally (never sent to the server), so a masked-out choice can't wedge an episode.
REWARD: terminal +/-WIN_REWARD (draw 0) + SHAPING * (dmg_dealt - dmg_taken)/START_HULL per round.
EPISODE: QUEUE -> MATCH_FOUND -> rounds -> MATCH_END; the next reset() re-QUEUEs on the same connection.

Optional imports: gymnasium/numpy. `python3 dw_env.py --smoke-test --port N` needs neither.
"""
import argparse
import random
import sys

import dw_rules as R
import dw_wire as W

try:  # optional
    import gymnasium as gym
    import numpy as np
    _Base = gym.Env
except ImportError:  # pragma: no cover
    gym = np = None
    _Base = object

OBS_DIM = 58
N_ACTIONS = 5
WIN_REWARD = 1.0
SHAPING = 0.1


def build_observation(rs, opp_kinds):
    o = [rs["hull_you"] / 20.0, rs["hull_opp"] / 20.0, rs["energy_you"] / 6.0, rs["energy_opp"] / 6.0, rs["round"] / 8.0]
    for c in rs["hand"]:
        v = [0.0] * 10; v[c if 0 <= c <= 8 else 9] = 1.0; o += v
    o.append(rs["opp_hand_size"] / 4.0)
    for k in opp_kinds:
        v = [0.0] * 4; v[k if 0 <= k <= 2 else 3] = 1.0; o += v
    assert len(o) == OBS_DIM
    return o


class DwEnv(_Base):
    def __init__(self, host="127.0.0.1", port=7000, name="dw-trainee", kind=W.KIND_BOT, timeout=30.0):
        self.host, self.port, self.name, self.kind, self.timeout = host, port, name, kind, timeout
        self.conn = None; self.rs = None; self.match = None; self.opp_kinds = [3, 3, 3]
        if gym is not None:
            self.observation_space = gym.spaces.Box(0.0, 1.0, (OBS_DIM,), np.float32)
            self.action_space = gym.spaces.Discrete(N_ACTIONS)

    def _out(self, obs): return np.asarray(obs, dtype=np.float32) if np is not None else obs

    def _connect(self):
        self.conn = W.Conn(self.host, self.port, self.timeout)
        self.conn.send(W.hello(self.name, self.kind))
        t, _ = self.conn.recv()
        if t != W.S_WELCOME: raise RuntimeError(f"expected WELCOME, got {t:#x}")

    def _next(self, want):
        while True:
            t, d = self.conn.recv()
            if t in want: return t, d
            if t == W.S_ERROR: raise RuntimeError(f"server ERROR {d}")
            if t == W.S_PLAY_REJECT: raise RuntimeError(f"PLAY_REJECT {d}")

    def reset(self, seed=None, options=None):
        if self.conn is None: self._connect()
        self.conn.send(W.queue())
        _, self.match = self._next({W.S_MATCH_FOUND})
        _, self.rs = self._next({W.S_ROUND_START})
        self.opp_kinds = [3, 3, 3]
        return self._out(build_observation(self.rs, self.opp_kinds)), {"opp_name": self.match["opp_name"], "match_id": self.match["match_id"]}

    def action_masks(self):
        return R.legal_mask(self.rs["hand"], self.rs["energy_you"])

    def step(self, action):
        mask = self.action_masks()
        a = int(action)
        slot = a if (a < 4 and mask[a]) else -1
        self.conn.send(W.play(self.match["match_id"], self.rs["round"], slot))
        _, rr = self._next({W.S_ROUND_RESULT})
        reward = SHAPING * (rr["dmg_to_opp"] - rr["dmg_to_you"]) / float(R.START_HULL)
        oc = rr["card_opp"]; self.opp_kinds = self.opp_kinds[1:] + [R.card_kind(oc) if oc >= 0 else 3]
        t, d = self._next({W.S_ROUND_START, W.S_MATCH_END})
        info = {"round_result": rr}
        if t == W.S_MATCH_END:
            reward += {1: WIN_REWARD, 0: -WIN_REWARD}.get(d["result"], 0.0)
            info.update(match_result=d["result"], match_end=d)
            return self._out(build_observation({**self.rs, "hull_you": rr["hull_you"], "hull_opp": rr["hull_opp"]}, self.opp_kinds)), reward, True, False, info
        self.rs = d
        return self._out(build_observation(d, self.opp_kinds)), reward, False, False, info

    def close(self):
        if self.conn: self.conn.close(); self.conn = None


def random_policy(mask, rng=random):
    return rng.choice([i for i, m in enumerate(mask) if m])


def play_episodes(env, n, policy=random_policy):
    """Stdlib-only rollout; returns list of match_result codes (0 loss, 1 win, 2 draw)."""
    out = []
    for _ in range(n):
        env.reset(); done = False
        while not done:
            _, _, done, _, info = env.step(policy(env.action_masks()))
        out.append(info["match_result"])
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--smoke-test", action="store_true"); ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, required=True); ap.add_argument("--episodes", type=int, default=3)
    a = ap.parse_args()
    e = DwEnv(a.host, a.port)
    res = play_episodes(e, a.episodes)
    print(f"smoke-test OK: {a.episodes} matches, results={res}")
    e.close()
