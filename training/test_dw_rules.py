import os, unittest
import dw_rules as R

VEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "parity_vectors.txt")
B = {"kind_beats", "is_legal_play", "match_decided"}


class Parity(unittest.TestCase):
    def test_all_vectors(self):
        n = 0
        with open(VEC) as f: lines = f.readlines()
        for line in lines:
            lhs, want = line.rsplit(" = ", 1)
            t = lhs.split(); fn = getattr(R, t[0]); args = [int(x) for x in t[1:]]
            got = fn(*args)
            if t[0] in B: got = int(got)
            self.assertEqual(got, int(want), line)
            n += 1
        self.assertGreater(n, 1000)

    def test_mask(self):
        self.assertEqual(R.legal_mask([2, 0, -1, 8], 2), [False, True, False, False, True])


if __name__ == "__main__":
    unittest.main()
