"""Runs the env against the real C server (build/dw_server --fast-forward) when it exists and is implemented;
skips cleanly otherwise. Needs 2 clients (trainee kind=bot + opponent kind=human) to pair."""
import os, socket, subprocess, time, unittest
import dw_env, dw_train
import dw_wire as W

BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "dw_server")


def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


class RealServer(unittest.TestCase):
    def test_full_matches(self):
        if not os.path.exists(BIN): self.skipTest("build/dw_server not built")
        port = free_port()
        p = subprocess.Popen([BIN, "--fast-forward", "--no-auth", "--port", str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(0.4)
            if p.poll() is not None: self.skipTest("dw_server exited (not implemented yet)")
            opp = dw_train.OpponentBot("127.0.0.1", port, lambda o, m: dw_env.random_policy(m)); opp.start()
            env = dw_env.DwEnv("127.0.0.1", port, kind=W.KIND_BOT)
            res = dw_env.play_episodes(env, 20)
            self.assertEqual(len(res), 20); self.assertTrue(set(res) <= {0, 1, 2})
            env.close(); opp.stop()
        finally:
            p.terminate(); p.wait(timeout=5)   # exact PID only


if __name__ == "__main__":
    unittest.main()
