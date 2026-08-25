#!/usr/bin/env python3
"""Dev server for the WASM build: serves web-dist/ plus a capture sink.

POST /upload with a data-URL (or raw) body writes the decoded payload to
captures/<name> and prints the path — the shell's captureFrame()/startClip()
helpers use it so harness code can pull frames out of the browser.
  python3 scripts/web_dev_server.py [port] [dist-dir]
"""
import base64
import http.server
import os
import sys
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8600
ROOT = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else
                       os.path.join(os.path.dirname(__file__), "..", "web-dist"))
CAPS = os.path.join(os.path.dirname(ROOT), "web-captures")


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=ROOT, **kw)

    def do_GET(self):
        # the launcher UI asks what bundles are stageable
        if self.path.split("?")[0] == "/bundles.json":
            import json
            bdir = os.path.join(ROOT, "bundles")
            names = sorted(f for f in os.listdir(bdir)
                           if f.endswith((".osb", ".osbui"))) if os.path.isdir(bdir) else []
            body = json.dumps(names).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def do_POST(self):
        if not self.path.startswith("/upload"):
            self.send_error(404)
            return
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            body = b""
            while True:
                size = int(self.rfile.readline().strip() or b"0", 16)
                if size == 0:
                    self.rfile.readline()
                    break
                body += self.rfile.read(size)
                self.rfile.readline()
        else:
            body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        ext = "bin"
        if body.startswith(b"data:"):
            head, _, data = body.partition(b",")
            ext = {"image/png": "png", "video/webm": "webm"}.get(
                head.split(b";")[0][5:].decode(), "bin")
            body = base64.b64decode(data)
        elif body[:8] == b"\x89PNG\r\n\x1a\n":
            ext = "png"
        elif body[:4] == b"\x1a\x45\xdf\xa3":
            ext = "webm"
        os.makedirs(CAPS, exist_ok=True)
        name = self.headers.get("X-Capture-Name") or f"cap_{int(time.time()*1000)}"
        # allow relative subdirs (eval capture runs upload frame sets), but
        # never let a path escape the captures root
        name = name.replace("\\", "/").lstrip("/")
        if ".." in name.split("/"):
            self.send_error(400)
            return
        if not name.endswith(f".{ext}"):
            name = f"{name}.{ext}"
        path = os.path.join(CAPS, name)
        os.makedirs(os.path.dirname(path) or CAPS, exist_ok=True)
        with open(path, "wb") as f:
            f.write(body)
        print(f"capture -> {path}", flush=True)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(path.encode())

    def end_headers(self):
        # dev iteration: never let the browser serve a stale build
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    print(f"serving {ROOT} on :{PORT}", flush=True)
    http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
