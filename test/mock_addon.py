#!/usr/bin/env python3
"""Mock Stremio addon + mock Stremio account API for host-side testing."""
import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import unquote

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
HASH = "abcdef0123456789abcdef0123456789abcdef01"
MANIFEST = {"id": "mock.addon", "name": "Mock éddon", "version": "1.0.0",
  "resources": ["catalog", "meta", {"name": "stream", "types": ["movie", "series"]}],
  "types": ["movie", "series"],
  "catalogs": [{"type": "movie", "id": "top", "name": "Top", "extra": [{"name": "search"}]},
               {"type": "series", "id": "top", "name": "Top Series"},
               {"type": "movie", "id": "find", "name": "Find", "extra": [{"name": "search", "isRequired": True}]},
               {"type": "movie", "id": "genre", "name": "NeedsGenre", "extra": [{"name": "genre", "isRequired": True}]}]}
GOOD_EMAIL, GOOD_PASS, KEY = "a@b.c", 'p"w\\d', "KEY123"

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json"); self.end_headers(); self.wfile.write(b)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        if self.path == "/api/login":
            if body.get("type") == "Login" and body.get("email") == GOOD_EMAIL and body.get("password") == GOOD_PASS:
                return self.send({"result": {"authKey": KEY, "user": {"email": GOOD_EMAIL}}})
            return self.send({"error": {"message": "Wrong e-mail or password"}}, 200)
        if self.path == "/api/addonCollectionGet":
            if body.get("type") == "AddonCollectionGet" and body.get("authKey") == KEY:
                return self.send({"result": {"addons": [
                    {"transportUrl": f"http://127.0.0.1:{PORT}/manifest.json", "manifest": {}},
                    {"transportUrl": "stremio://example.com/foo/manifest.json", "manifest": {}},
                    {"transportUrl": "ftp://bad", "manifest": {}}]}})
            return self.send({"error": "Session does not exist"})
        self.send_response(404); self.end_headers()

    def do_GET(self):
        p = unquote(self.path)
        if p == "/manifest.json": return self.send(MANIFEST)
        if p == "/catalog/movie/top.json":
            return self.send({"metas": [{"id": "tt1", "type": "movie", "name": "Movie One ☃", "poster": f"http://127.0.0.1:{PORT}/poster.bin", "releaseInfo": "2020"},
                                        {"id": "tt2", "type": "movie", "name": "O'Reilly \"Quoted\" Movie", "releaseInfo": 2021}]})
        if p == "/catalog/series/top.json":
            return self.send({"metas": [{"id": "ttS", "type": "series", "name": "Show"}]})
        if p.startswith("/catalog/movie/top/search=") or p.startswith("/catalog/movie/find/search="):
            q = p.split("search=", 1)[1][:-5]
            return self.send({"metas": [{"id": "tt1", "type": "movie", "name": "Result for " + q}]})
        if p == "/meta/movie/tt1.json":
            return self.send({"meta": {"id": "tt1", "type": "movie", "name": "Movie One", "description": "A film.\nWith lines.", "releaseInfo": "2020"}})
        if p == "/meta/series/ttS.json":
            return self.send({"meta": {"id": "ttS", "type": "series", "name": "Show", "videos": [
                {"id": "ttS:1:1", "title": "Pilot", "season": 1, "episode": 1},
                {"id": "ttS:1:2", "name": "Second", "season": 1, "episode": 2},
                {"id": "ttS:0:1", "title": "Special", "season": 0, "episode": 1}]}})
        if p == "/stream/movie/tt1.json" or p == "/stream/series/ttS:1:2.json":
            return self.send({"streams": [
                {"url": "https://example.com/a.mp4", "name": "Direct\n1080p", "title": "First line\nsecond line"},
                {"infoHash": HASH.upper(), "fileIdx": 2, "name": "Tor", "title": "Seeds 50\nmore",
                 "sources": ["tracker:udp://t.example:1337/announce", "dht:" + HASH]},
                {"ytId": "xyz", "name": "YouTube"},
                {"url": "ftp://nope/x", "name": "ftp"},
                {"url": "http://example.com/b.mkv"}]})
        if p == "/poster.bin":
            self.send_response(200); self.end_headers(); self.wfile.write(b"POSTERDATA"); return
        self.send_response(404); self.end_headers()

HTTPServer(("127.0.0.1", PORT), H).serve_forever()
