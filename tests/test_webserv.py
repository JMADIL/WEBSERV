"""Small end-to-end checks for the configured Webserv instance.

Run with `make test`. The suite binds an unused localhost port, never 8080.
"""

import concurrent.futures
import http.client
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]


class WebservTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        config = (ROOT / "configs/default.conf").read_text()
        original_listen = "listen 127.0.0.1:8080;"
        if config.count(original_listen) != 1:
            raise RuntimeError("expected one default.conf localhost listener")

        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            cls.port = probe.getsockname()[1]

        with tempfile.NamedTemporaryFile(mode="w", suffix=".conf", delete=False) as temp:
            temp.write(config.replace(original_listen,
                                      "listen 127.0.0.1:%d;" % cls.port, 1))
            cls.config_path = Path(temp.name)

        cls.server = None
        try:
            cls.server = subprocess.Popen(
                [str(ROOT / "webserv"), str(cls.config_path)],
                cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            )
            for _ in range(50):
                if cls.server.poll() is not None:
                    raise RuntimeError("webserv exited: " + cls.server.stderr.read().decode())
                try:
                    with socket.create_connection(("127.0.0.1", cls.port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("webserv did not start within five seconds")
        except Exception:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        server = getattr(cls, "server", None)
        if server is not None:
            if server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)
            server.stderr.close()
        config_path = getattr(cls, "config_path", None)
        if config_path is not None:
            config_path.unlink(missing_ok=True)

    def request(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, response.read()
        finally:
            connection.close()

    def raw_status(self, payload):
        with socket.create_connection(("127.0.0.1", self.port), timeout=5) as conn:
            conn.settimeout(5)
            conn.sendall(payload)
            line = conn.recv(4096).split(b"\r\n", 1)[0]
        return int(line.split(b" ", 2)[1])

    def test_pages_and_python_cgi(self):
        status, body = self.request("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(b"<html", body)
        status, body = self.request("GET", "/uploads")
        self.assertEqual(status, 200)
        self.assertIn(b"Upload file", body)
        status, body = self.request("GET", "/cgi-bin/hello.py?from=test")
        self.assertEqual(status, 200)
        self.assertIn(b"Python CGI works", body)
        self.assertIn(b"from=test", body)

    def test_bad_request_and_body_limit(self):
        self.assertEqual(self.raw_status(b"GET / HTTP/1.1\r\nConnection: close\r\n\r\n"), 400)
        oversized = (b"POST /uploads/test HTTP/1.1\r\nHost: localhost\r\n"
                     b"Content-Length: 10485761\r\nConnection: close\r\n\r\n")
        self.assertEqual(self.raw_status(oversized), 413)

    def test_request_line_limit(self):
        for length, expected in ((8192, 404), (8193, 400)):
            line = b"GET /" + b"a" * (length - len(b"GET /") - len(b" HTTP/1.1")) + b" HTTP/1.1"
            self.assertEqual(len(line), length)
            request = line + b"\r\nHost: localhost\r\nConnection: close\r\n\r\n"
            with self.subTest(length=length):
                self.assertEqual(self.raw_status(request), expected)

    def test_binary_upload_round_trip(self):
        name = "webserv_test_" + uuid.uuid4().hex + ".bin"
        path = ROOT / "www/uploads" / name
        self.assertFalse(path.exists())
        self.addCleanup(lambda: path.unlink(missing_ok=True))
        data = b"\x00\xff\r\n" + os.urandom(1024)
        self.assertEqual(self.request("POST", "/uploads/" + name, data)[0], 201)
        self.assertEqual(self.request("GET", "/uploads/" + name), (200, data))
        self.assertEqual(self.request("DELETE", "/uploads/" + name)[0], 204)
        self.assertEqual(self.request("GET", "/uploads/" + name)[0], 404)

    def test_concurrent_static_requests(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=10) as pool:
            statuses = list(pool.map(lambda _: self.request("GET", "/")[0], range(40)))
        self.assertEqual(statuses, [200] * 40)


if __name__ == "__main__":
    unittest.main()
