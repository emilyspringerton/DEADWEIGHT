import shutil, tempfile, unittest
import dw_train
from rl_league import ALL_ROLES, LeagueManager


class DryRun(unittest.TestCase):
    def test_two_generations_register_six_members_and_elo_moves(self):
        d = tempfile.mkdtemp()
        try:
            dw_train.main(["--dry-run", "--generations", "3", "--episodes", "8", "--league-dir", d, "--seed", "3"])
            lg = LeagueManager(d)
            ms = lg.all_members()
            self.assertEqual(len(ms), 9)   # 3 generations x 3 roles, atomically
            for r in ALL_ROLES: self.assertEqual(len(lg.members_by_role(r)), 3)
            self.assertTrue(any(lg.get_elo(m.id) != 1500.0 for m in ms))  # generations >1 play league members
            self.assertEqual(len(dw_train.rl_registry.LocalRegistry(d + "/registry_local").list()), 9)
        finally:
            shutil.rmtree(d)

    def test_resume_continues_generation_numbering(self):
        d = tempfile.mkdtemp()
        try:
            dw_train.main(["--dry-run", "--generations", "1", "--episodes", "2", "--league-dir", d])
            dw_train.main(["--dry-run", "--generations", "1", "--episodes", "2", "--league-dir", d])
            self.assertEqual(sorted({m.generation for m in LeagueManager(d).all_members()}), [1, 2])
        finally:
            shutil.rmtree(d)

    def test_resume_from_registry_seeds_a_fresh_league_and_continues_numbering(self):
        a, b = tempfile.mkdtemp(), tempfile.mkdtemp()
        try:
            dw_train.main(["--dry-run", "--generations", "2", "--episodes", "2", "--league-dir", a])
            shutil.rmtree(b + "/registry_local", ignore_errors=True)
            shutil.copytree(a + "/registry_local", b + "/registry_local")      # a "recycled runtime" that only has the registry
            dw_train.main(["--dry-run", "--generations", "1", "--episodes", "2", "--league-dir", b, "--resume-from-registry"])
            gens = sorted({m.generation for m in LeagueManager(b).all_members()})
            self.assertEqual(gens, [2, 3])   # gen 2 = the resumed snapshot, gen 3 = this run continues the same league
        finally:
            shutil.rmtree(a); shutil.rmtree(b)

    def test_registry_failures_never_kill_training(self):
        class Broken:
            def list(self, role=None): raise RuntimeError("route not deployed (404)")
            def push(self, *a, **k): raise RuntimeError("boom")
        d = tempfile.mkdtemp()
        try:
            self.assertEqual(dw_train.resume_from_registry(Broken(), LeagueManager(d), d, {}, True), 0)
        finally:
            shutil.rmtree(d)


if __name__ == "__main__":
    unittest.main()
