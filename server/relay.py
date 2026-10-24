#!/usr/bin/env python3
"""Notch share relay: stores opaque (end-to-end encrypted) files for a limited time.

Standard library only. Configuration comes from environment variables (see .env.example).
The relay never sees file names or keys: the key travels in the link's #fragment.
"""
import hashlib
import hmac
import json
import os
import re
import secrets
import shutil
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

PAGE = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "page.html"), "rb").read()
ID_RE = re.compile(r"^[A-Za-z0-9_-]{16,64}$")
PIN_MAX_FAILS, PIN_LOCK_SECONDS = 5, 600


class Config:
    def __init__(self, env=os.environ):
        self.api_key = env.get("RELAY_API_KEY", "")
        self.data_dir = env.get("DATA_DIR", "/data")
        self.max_file = int(float(env.get("MAX_FILE_MB", "500")) * 1024 * 1024)
        self.max_total = int(float(env.get("MAX_TOTAL_MB", "2000")) * 1024 * 1024)
        self.max_ttl = int(float(env.get("MAX_TTL_HOURS", "24")) * 3600)
        self.trust_proxy = env.get("TRUST_PROXY", "0") == "1"
        self.host = env.get("BIND", "0.0.0.0")
        self.port = int(env.get("PORT", "8080"))


class Store:
    """Shares live in <data>/<id>/{blob,meta.json}; uploads are staged in <data>/tmp."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.lock = threading.Lock()
        self.fails = {}  # (ip, id) -> (count, locked_until)
        shutil.rmtree(self.tmp, ignore_errors=True)
        os.makedirs(self.tmp, exist_ok=True)

    @property
    def tmp(self):
        return os.path.join(self.cfg.data_dir, "tmp")

    def path(self, sid, name=""):
        return os.path.join(self.cfg.data_dir, sid, name)

    def meta(self, sid):
        try:
            with open(self.path(sid, "meta.json")) as f:
                m = json.load(f)
        except (OSError, ValueError):
            return None
        if m["expires_at"] <= time.time() or m["downloads"] >= m["max_downloads"] > 0:
            self.delete(sid)
            return None
        return m

    def save_meta(self, sid, m):
        tmp = self.path(sid, "meta.json.tmp")
        with open(tmp, "w") as f:
            json.dump(m, f)
        os.replace(tmp, self.path(sid, "meta.json"))

    def delete(self, sid):
        shutil.rmtree(self.path(sid), ignore_errors=True)

    def used(self):
        total = 0
        for sid in os.listdir(self.cfg.data_dir):
            try:
                total += os.path.getsize(self.path(sid, "blob"))
            except OSError:
                pass
        return total

    def sweep(self):
        for sid in os.listdir(self.cfg.data_dir):
            if ID_RE.match(sid):
                self.meta(sid)  # deletes it when expired or used up

    def count_download(self, sid):
        with self.lock:
            m = self.meta(sid)
            if m:
                m["downloads"] += 1
                self.save_meta(sid, m)

    def pin_ok(self, sid, ip, pin, m):
        if not m["pin_hash"]:
            return True
        key = (ip, sid)
        with self.lock:
            count, locked = self.fails.get(key, (0, 0))
            if locked > time.time():
                return False
            good = hmac.compare_digest(
                m["pin_hash"], hashlib.pbkdf2_hmac("sha256", pin.encode(), bytes.fromhex(m["pin_salt"]), 100_000).hex())
            if good:
                self.fails.pop(key, None)
                return True
            count += 1
            self.fails[key] = (0, time.time() + PIN_LOCK_SECONDS) if count >= PIN_MAX_FAILS else (count, 0)
            return False

    def pin_locked(self, sid, ip):
        return self.fails.get((ip, sid), (0, 0))[1] > time.time()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "NotchRelay"
    sys_version = ""
    timeout = 60

    def log_message(self, fmt, *args):  # keep tokens out of the logs: log method and status only
        sys.stderr.write("%s %s\n" % (self.command, args[1] if len(args) > 1 else ""))

    @property
    def store(self):
        return self.server.store

    def ip(self):
        if self.server.cfg.trust_proxy:
            fwd = self.headers.get("X-Forwarded-For", "")
            if fwd:
                return fwd.split(",")[-1].strip()
        return self.client_address[0]

    def reply(self, code, body=b"", ctype="application/json", extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Content-Type-Options", "nosniff")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def authed(self):
        got = self.headers.get("Authorization", "")
        want = "Bearer " + self.server.cfg.api_key
        return hmac.compare_digest(got.encode(), want.encode())

    # ---- routes -------------------------------------------------------------------------
    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/v1/ping":
            if not self.authed():
                return self.reply(401, {"error": "bad key"})
            c = self.server.cfg
            return self.reply(200, {"ok": True, "max_file": c.max_file, "max_ttl": c.max_ttl,
                                    "free": max(0, c.max_total - self.store.used())})
        m = re.match(r"^/api/v1/shares/([^/]+)$", path)
        if m and ID_RE.match(m.group(1)):  # progress check for the sender
            if not self.authed():
                return self.reply(401, {"error": "bad key"})
            info = self.store.meta(m.group(1))
            if not info:
                return self.reply(404, {"error": "gone"})
            return self.reply(200, {"downloads": info["downloads"], "max_downloads": info["max_downloads"],
                                    "expires_at": info["expires_at"]})
        m = re.match(r"^/s/([^/]+)(/meta|/blob)?$", path)
        if not m or not ID_RE.match(m.group(1)):
            return self.reply(404, {"error": "not found"})
        sid, part = m.group(1), m.group(2)
        info = self.store.meta(sid)
        if part is None:
            csp = ("default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                   "connect-src 'self'; img-src blob: data:; frame-ancestors 'none'")
            return self.reply(200 if info else 404, PAGE, "text/html; charset=utf-8",
                              {"Content-Security-Policy": csp})
        if not info:
            return self.reply(404, {"error": "expired"})
        if part == "/meta":
            # now: lets the page count down against the server's clock, not the phone's
            return self.reply(200, {"size": info["size"], "pin_required": bool(info["pin_hash"]),
                                    "expires_at": info["expires_at"], "now": int(time.time()),
                                    "single_use": info["max_downloads"] == 1})
        if self.store.pin_locked(sid, self.ip()):
            return self.reply(429, {"error": "too many tries"})
        if not self.store.pin_ok(sid, self.ip(), self.headers.get("X-Pin", ""), info):
            return self.reply(403, {"error": "wrong pin"})
        self.send_blob(sid, info)

    do_HEAD = do_GET

    def send_blob(self, sid, info):
        size = info["size"]
        start, end, code = 0, size - 1, 200
        rng = re.match(r"^bytes=(\d*)-(\d*)$", self.headers.get("Range", ""))
        if rng and (rng.group(1) or rng.group(2)):
            if rng.group(1):
                start = int(rng.group(1))
                end = int(rng.group(2)) if rng.group(2) else size - 1
            else:
                start = max(0, size - int(rng.group(2)))
            end = min(end, size - 1)
            if start > end:
                return self.reply(416, b"", extra={"Content-Range": "bytes */%d" % size})
            code = 206
        extra = {"Accept-Ranges": "bytes", "Content-Disposition": "attachment; filename=share.bin"}
        if code == 206:
            extra["Content-Range"] = "bytes %d-%d/%d" % (start, end, size)
        self.send_response(code)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Cache-Control", "no-store")
        for k, v in extra.items():
            self.send_header(k, v)
        self.end_headers()
        if self.command == "HEAD":
            return
        left = end - start + 1
        with open(self.store.path(sid, "blob"), "rb") as f:
            f.seek(start)
            while left > 0:
                chunk = f.read(min(65536, left))
                if not chunk:
                    return
                self.wfile.write(chunk)
                left -= len(chunk)
        if end == size - 1:
            self.store.count_download(sid)

    def do_POST(self):
        if urlparse(self.path).path != "/api/v1/shares":
            return self.reply(404, {"error": "not found"})
        if not self.authed():
            return self.reply(401, {"error": "bad key"})
        cfg = self.server.cfg
        try:
            length = int(self.headers.get("Content-Length", ""))
            ttl = min(int(self.headers.get("X-TTL", "3600")), cfg.max_ttl)
            max_dl = max(0, int(self.headers.get("X-Max-Downloads", "0")))
        except ValueError:
            return self.reply(400, {"error": "bad headers"})
        if length <= 0 or ttl <= 0:
            return self.reply(400, {"error": "bad headers"})
        if length > cfg.max_file or self.store.used() + length > cfg.max_total:
            self.close_connection = True
            return self.reply(413, {"error": "too large or relay is full"})
        pin = self.headers.get("X-Pin", "")
        salt = secrets.token_bytes(16)
        sid = secrets.token_urlsafe(16)
        staged = os.path.join(self.store.tmp, sid)
        got = 0
        try:
            with open(staged, "wb") as f:
                while got < length:
                    chunk = self.rfile.read(min(65536, length - got))
                    if not chunk:
                        break
                    f.write(chunk)
                    got += len(chunk)
            if got != length:
                raise OSError("short upload")
            os.makedirs(self.store.path(sid))
            os.replace(staged, self.store.path(sid, "blob"))
        except OSError:
            if os.path.exists(staged):
                os.remove(staged)
            self.store.delete(sid)
            self.close_connection = True
            return self.reply(400, {"error": "upload failed"})
        expires = int(time.time()) + ttl
        self.store.save_meta(sid, {
            "size": length, "expires_at": expires, "max_downloads": max_dl, "downloads": 0,
            "pin_salt": salt.hex() if pin else "",
            "pin_hash": hashlib.pbkdf2_hmac("sha256", pin.encode(), salt, 100_000).hex() if pin else "",
        })
        self.reply(201, {"id": sid, "expires_at": expires})

    def do_DELETE(self):
        m = re.match(r"^/api/v1/shares/([^/]+)$", urlparse(self.path).path)
        if not m or not ID_RE.match(m.group(1)):
            return self.reply(404, {"error": "not found"})
        if not self.authed():
            return self.reply(401, {"error": "bad key"})
        self.store.delete(m.group(1))
        self.reply(200, {"ok": True})


def make_server(cfg):
    os.makedirs(cfg.data_dir, exist_ok=True)
    srv = ThreadingHTTPServer((cfg.host, cfg.port), Handler)
    srv.cfg, srv.store = cfg, Store(cfg)
    srv.daemon_threads = True

    def cleaner():
        while True:
            time.sleep(60)
            srv.store.sweep()
    threading.Thread(target=cleaner, daemon=True).start()
    return srv


if __name__ == "__main__":
    config = Config()
    if len(config.api_key) < 32:
        sys.exit("RELAY_API_KEY must be set to at least 32 characters (openssl rand -hex 32)")
    server = make_server(config)
    print("Notch relay listening on %s:%d" % (config.host, config.port), flush=True)
    server.serve_forever()
