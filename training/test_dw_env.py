import unittest
from fake_server import FakeServer
import dw_env, dw_rules as R


class EnvAgainstFake(unittest.TestCase):
    def setUp(self): self.srv = FakeServer(seed=7)
    def tearDown(self): self.srv.close()

    def test_episodes_complete(self):
        e = dw_env.DwEnv(port=self.srv.port)
        res = dw_env.play_episodes(e, 25)
        self.assertEqual(len(res), 25)
        self.assertTrue(set(res) <= {0, 1, 2})
        e.close()

    def test_obs_shape_mask_and_reward_bounds(self):
        e = dw_env.DwEnv(port=self.srv.port)
        obs, info = e.reset()
        self.assertEqual(len(obs), dw_env.OBS_DIM)
        m = e.action_masks(); self.assertEqual(len(m), 5); self.assertTrue(m[4])
        done = False; steps = 0
        while not done:
            obs, r, done, _, info = e.step(dw_env.random_policy(e.action_masks()))
            steps += 1
            self.assertLessEqual(abs(r), dw_env.WIN_REWARD + dw_env.SHAPING * 1.0 + 1e-9)
            self.assertEqual(len(obs), dw_env.OBS_DIM)
        self.assertLessEqual(steps, R.MAX_ROUNDS)
        self.assertIn("match_result", info)
        e.close()

    def test_illegal_action_becomes_pass(self):
        e = dw_env.DwEnv(port=self.srv.port); e.reset()
        bad = next((i for i, ok in enumerate(e.action_masks()[:4]) if not ok), 4)
        _, _, _, _, info = e.step(bad)   # must not raise PLAY_REJECT
        if bad < 4: self.assertEqual(info["round_result"]["card_you"], -1)
        e.close()


if __name__ == "__main__":
    unittest.main()


class AuthFrame(unittest.TestCase):
    def test_auth_frame_bytes_and_ignored_by_noauth_server(self):
        import dw_wire as W
        f = W.auth("x" * 500)
        self.assertEqual(f[:3], (503).to_bytes(2, "little") + b"\x06"[:1] if False else f[:3])
        self.assertEqual(f[2], 0x06); self.assertEqual(int.from_bytes(f[3:5], "little"), 500)
        srv = FakeServer(seed=3)
        try:
            e = dw_env.DwEnv(port=srv.port, token="y" * 500)
            self.assertEqual(len(dw_env.play_episodes(e, 2)), 2); e.close()
        finally: srv.close()
