import http.server, json, os, tempfile, threading, unittest
import rl_registry as RR


class H(http.server.BaseHTTPRequestHandler):
    seen = []
    def log_message(self, *a): pass
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        H.seen.append((self.path, self.headers.get("Authorization"), body))
        out = {"access_token": "T"} if self.path == "/api/v1/auth/agent" else {"id": 1, "role": "main"}
        b = json.dumps(out).encode(); self.send_response(200); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        H.seen.append((self.path, None, b""))
        b = b"[]"; self.send_response(200); self.send_header("Content-Length", "2"); self.end_headers(); self.wfile.write(b)


class T(unittest.TestCase):
    def test_multipart(self):
        body, ct = RR._multipart_body({"role": "main"}, [("file", "a.zip", b"xx")])
        self.assertIn(b'name="file"; filename="a.zip"', body); self.assertIn("boundary=", ct)

    def test_local_fallback(self):
        with tempfile.TemporaryDirectory() as d:
            ck = os.path.join(d, "c.zip"); open(ck, "wb").write(b"z")
            r = RR.make_registry(local_dir=os.path.join(d, "reg"))
            r.push("main", 1, 1500.0, "here", ck); r.push("main_exploiter", 1, 1500.0, "here", ck)
            self.assertEqual(len(r.list()), 2); self.assertEqual(len(r.list("main")), 1)

    def test_remote_uses_game_scoped_path_and_bearer(self):
        srv = http.server.HTTPServer(("127.0.0.1", 0), H); threading.Thread(target=srv.serve_forever, daemon=True).start()
        base = f"http://127.0.0.1:{srv.server_port}"
        with tempfile.TemporaryDirectory() as d:
            ck = os.path.join(d, "c.zip"); open(ck, "wb").write(b"z")
            H.seen.clear()
            r = RR.make_registry(base_url=base, agent_secret="s"); r.push("main", 2, 1500.0, "colab", ck); r.list("main")
        srv.shutdown()
        paths = [p for p, _, _ in H.seen]
        self.assertIn("/api/v1/game-checkpoints/deadweight", paths)
        self.assertIn("/api/v1/game-checkpoints/deadweight?role=main", paths)
        self.assertTrue(any(a == "Bearer T" for _, a, _ in H.seen))


if __name__ == "__main__":
    unittest.main()
