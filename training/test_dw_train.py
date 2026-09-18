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


if __name__ == "__main__":
    unittest.main()
