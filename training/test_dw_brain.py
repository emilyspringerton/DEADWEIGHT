"""Tests for the hybrid bot brain's Python side + cross-language parity with the C/PARENA implementation."""
import math
import os
import random
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dw_brain as B
import dw_env as E
import dw_rules as R

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DUMP = os.path.join(ROOT, "build", "brain_dump")


def rand_layers(rng, h=8):
    def mk(o, i, a): return ([[rng.uniform(-0.5, 0.5) for _ in range(i)] for _ in range(o)], [rng.uniform(-0.3, 0.3) for _ in range(o)], a)
    return [mk(h, B.OBS, 1), mk(h, h, 1), mk(B.ACT, h, 0)]


class TestBrain(unittest.TestCase):
    def test_tanh_approx_accuracy(self):
        worst = max(abs(B.tanh_approx(x / 100.0) - math.tanh(x / 100.0)) for x in range(-800, 801))
        self.assertLess(worst, 2e-3)
        self.assertEqual(B.tanh_approx(9.0), 1.0); self.assertEqual(B.tanh_approx(-9.0), -1.0)

    def test_blob_roundtrip_and_strictness(self):
        L = rand_layers(random.Random(1)); blob = B.write_blob(None, L); L2 = B.read_blob(blob)
        self.assertEqual([len(w[0]) for w, _, _ in L2], [B.OBS, 8, 8]); self.assertEqual(B.read_blob(B.write_blob(None, L2)), L2)
        with self.assertRaises(Exception): B.read_blob(blob + b"\0")
        with self.assertRaises(Exception): B.read_blob(b"XXXX" + blob[4:])

    def test_teacher_prefers_counter(self):
        rs = {"hull_you": 20, "hull_opp": 20, "energy_you": 6, "energy_opp": 4, "round": 3, "hand": [0, 3, 6, 8], "opp_hand_size": 4}
        t = B.teacher(rs, [3, 3, 0])   # opponent just played BURST, will likely repeat: SHIELD (6..8) counters it
        best = max(range(4), key=lambda i: t[i])
        self.assertEqual(R.card_kind(rs["hand"][best]), 2)   # a SHIELD (either tier) is the counter

    @unittest.skipUnless(os.path.exists(DUMP), "build/brain_dump not built (scripts/build.sh builds it)")
    def test_c_parity_obs_and_logits(self):
        rng = random.Random(5)
        L = B.read_blob(B.write_blob(None, rand_layers(rng, 16)))   # float32-quantised, exactly what C reads
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f: path = f.name
        B.write_blob(path, L)
        states, lines = [], []
        for _ in range(300):
            rs, kinds = B.random_state(rng); states.append((rs, kinds))
            lines.append(" ".join(map(str, [rs["hull_you"], rs["hull_opp"], rs["energy_you"], rs["energy_opp"], rs["round"], *rs["hand"], rs["opp_hand_size"], *kinds])))
        out = subprocess.run([DUMP, path], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True).stdout.splitlines()
        os.unlink(path)
        self.assertEqual(len(out), 600)
        worst = 0.0
        for i, (rs, kinds) in enumerate(states):
            c_obs = list(map(float, out[2 * i].split()[1:])); c_log = list(map(float, out[2 * i + 1].split()[1:]))
            py_obs = E.build_observation(rs, kinds)
            self.assertEqual(c_obs, py_obs, "observation layout drifted between C glue and dw_env.py")
            py_log = B.forward(L, py_obs); worst = max(worst, max(abs(a - b) for a, b in zip(c_log, py_log)))
        self.assertLess(worst, 1e-9, f"PARENA/C forward pass vs Python reference: max |diff| {worst}")

    def test_distill_is_deterministic_smoke(self):
        try: import numpy  # noqa: F401
        except ImportError: self.skipTest("numpy not installed")
        a = B.distill(600, 2, 8, 3); b = B.distill(600, 2, 8, 3)
        self.assertEqual(B.write_blob(None, a), B.write_blob(None, b))


if __name__ == "__main__":
    unittest.main()
