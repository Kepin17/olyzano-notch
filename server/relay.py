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
import sqlite3
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = open(os.path.join(HERE, "page.html"), "rb").read()
JOIN_PAGE = open(os.path.join(HERE, "join.html"), "rb").read()
ID_RE = re.compile(r"^[A-Za-z0-9_-]{16,64}$")
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._ -]{0,39}$")
PIN_MAX_FAILS, PIN_LOCK_SECONDS = 5, 600
INVITE_DAYS = 7
MB = 1024 * 1024
OWNER = 0  # the built-in member behind RELAY_API_KEY: the host, unlimited by member quotas


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
        self.public_url = env.get("PUBLIC_URL", "").rstrip("/")  # only used to print invite links


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


class Members:
    """Who may upload, and how much. A SQLite file next to the shares; the command line and the server
    both open it, so every call uses its own short connection."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.path = os.path.join(cfg.data_dir, "relay.db")
        os.makedirs(cfg.data_dir, exist_ok=True)
        with self.db() as c:
            c.execute("""CREATE TABLE IF NOT EXISTS members(
                id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, key_hash TEXT, created INTEGER NOT NULL,
                revoked INTEGER NOT NULL DEFAULT 0, quota_mb INTEGER, max_file_mb INTEGER,
                max_ttl_hours INTEGER, max_active INTEGER)""")
            c.execute("""CREATE TABLE IF NOT EXISTS invites(
                token_hash TEXT PRIMARY KEY, member_id INTEGER NOT NULL, expires_at INTEGER NOT NULL,
                used INTEGER NOT NULL DEFAULT 0)""")
        self.fails = {}  # ip -> (count, locked_until) for /join
        self.lock = threading.Lock()

    def db(self):
        c = sqlite3.connect(self.path, timeout=10)
        c.row_factory = sqlite3.Row
        return c

    def find(self, key):
        """The member behind a bearer key, or None. The host's own key is member OWNER."""
        if self.cfg.api_key and hmac.compare_digest(key.encode(), self.cfg.api_key.encode()):
            return {"id": OWNER, "name": "owner", "quota_mb": None, "max_file_mb": None,
                    "max_ttl_hours": None, "max_active": None}
        if not key:
            return None
        with self.db() as c:
            row = c.execute("SELECT * FROM members WHERE key_hash = ? AND revoked = 0", (sha(key),)).fetchone()
        return dict(row) if row else None

    def get(self, name):
        with self.db() as c:
            row = c.execute("SELECT * FROM members WHERE name = ?", (name,)).fetchone()
        return dict(row) if row else None

    def all(self):
        with self.db() as c:
            return [dict(r) for r in c.execute("SELECT * FROM members ORDER BY id")]

    def limits(self, m):
        """What this member may do: their own limits, never above the server's."""
        cfg = self.cfg

        def cap(value, unit, ceiling):
            return min(value * unit, ceiling) if value else ceiling
        return {"quota": cap(m.get("quota_mb"), MB, cfg.max_total),
                "max_file": cap(m.get("max_file_mb"), MB, cfg.max_file),
                "max_ttl": cap(m.get("max_ttl_hours"), 3600, cfg.max_ttl),
                "max_active": m.get("max_active") or 0}

    def add(self, name, **limits):
        if not NAME_RE.match(name):
            raise ValueError("a name is 1-40 letters, digits, space . _ -")
        with self.db() as c:
            try:
                cur = c.execute("INSERT INTO members(name, created, quota_mb, max_file_mb, max_ttl_hours, max_active)"
                                " VALUES(?,?,?,?,?,?)", (name, int(time.time()), limits.get("quota_mb"),
                                                         limits.get("max_file_mb"), limits.get("max_ttl_hours"),
                                                         limits.get("max_active")))
            except sqlite3.IntegrityError:
                raise ValueError("there is already a member called %r" % name)
            return cur.lastrowid

    def update(self, name, **limits):
        sets = {k: v for k, v in limits.items() if v is not None}
        if not sets:
            return
        with self.db() as c:
            c.execute("UPDATE members SET " + ", ".join("%s = ?" % k for k in sets) + " WHERE name = ?",
                      (*sets.values(), name))

    def revoke(self, name):
        with self.db() as c:
            c.execute("UPDATE members SET revoked = 1, key_hash = NULL WHERE name = ?", (name,))
            c.execute("UPDATE invites SET used = 1 WHERE member_id IN (SELECT id FROM members WHERE name = ?)", (name,))

    def invite(self, name):
        """A single-use token (valid INVITE_DAYS) that hands the member a fresh key. Reissuing also resets the key."""
        member = self.get(name)
        if not member or member["revoked"]:
            raise ValueError("no active member called %r" % name)
        token = secrets.token_urlsafe(24)
        with self.db() as c:
            c.execute("INSERT INTO invites VALUES(?,?,?,0)", (sha(token), member["id"], int(time.time()) + INVITE_DAYS * 86400))
        return token

    def redeem(self, token, ip):
        """-> (name, key), or None. A new key replaces any older one of that member. Guessing is rate limited."""
        with self.lock:
            count, locked = self.fails.get(ip, (0, 0))
            if locked > time.time():
                return "locked"
        with self.db() as c:
            row = c.execute("""SELECT i.*, m.name, m.revoked FROM invites i JOIN members m ON m.id = i.member_id
                               WHERE i.token_hash = ? AND i.used = 0 AND i.expires_at > ?""", (sha(token), int(time.time()))).fetchone()
            if not row or row["revoked"]:
                with self.lock:
                    count += 1
                    self.fails[ip] = (0, time.time() + PIN_LOCK_SECONDS) if count >= 10 else (count, 0)
                return None
            key = secrets.token_hex(32)
            c.execute("UPDATE invites SET used = 1 WHERE token_hash = ?", (row["token_hash"],))
            c.execute("UPDATE members SET key_hash = ? WHERE id = ?", (sha(key), row["member_id"]))
        return row["name"], key

    def invite_link(self, token):
        return "%s/join#%s" % (self.cfg.public_url or "https://YOUR-SERVER", token)


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

    def used(self, owner=None):
        """Bytes of live shares: all of them, or only one member's."""
        total = 0
        for sid in os.listdir(self.cfg.data_dir):
            if not ID_RE.match(sid):
                continue
            m = self.meta(sid)
            if m and (owner is None or m.get("owner", OWNER) == owner):
                total += m["size"]
        return total

    def active(self, owner):
        return sum(1 for sid in os.listdir(self.cfg.data_dir)
                   if ID_RE.match(sid) and (m := self.meta(sid)) and m.get("owner", OWNER) == owner)

    def shares(self):
        out = []
        for sid in sorted(os.listdir(self.cfg.data_dir)):
            if ID_RE.match(sid) and (m := self.meta(sid)):
                out.append((sid, m))
        return out

    def sweep(self):
        for sid in os.listdir(self.cfg.data_dir):
            if ID_RE.match(sid):
                self.meta(sid)  # deletes it when expired or used up

    def reserve(self, sid):
        """Claim a download just before the last byte range goes out, atomically with the single-use check, so
        two requests racing for a single-use link cannot both win. Give it back with release() if the
        transfer breaks."""
        with self.lock:
            m = self.meta(sid)  # None once a single-use link is used up
            if not m:
                return False
            m["downloads"] += 1
            self.save_meta(sid, m)
            return True

    def release(self, sid):
        with self.lock:
            try:
                with open(self.path(sid, "meta.json")) as f:
                    m = json.load(f)
                m["downloads"] = max(0, m["downloads"] - 1)
                self.save_meta(sid, m)
            except (OSError, ValueError):
                pass

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

    def auth(self):
        """The member sending this request, or None."""
        got = self.headers.get("Authorization", "")
        return self.server.members.find(got[7:]) if got.startswith("Bearer ") else None

    def own(self, member, info):
        """A member sees only their own shares; the host (OWNER) sees all."""
        return info is not None and (member["id"] == OWNER or info.get("owner", OWNER) == member["id"])

    # ---- routes -------------------------------------------------------------------------
    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/join":
            csp = "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'"
            return self.reply(200, JOIN_PAGE, "text/html; charset=utf-8", {"Content-Security-Policy": csp})
        if path == "/api/v1/ping":
            member = self.auth()
            if not member:
                return self.reply(401, {"error": "bad key"})
            c, lim = self.server.cfg, self.server.members.limits(member)
            used, active = self.store.used(member["id"]), self.store.active(member["id"])
            free = max(0, min(lim["quota"] - used, c.max_total - self.store.used()))
            return self.reply(200, {"ok": True, "member": member["name"], "max_file": lim["max_file"],
                                    "max_ttl": lim["max_ttl"], "free": free,
                                    "limits": lim, "usage": {"used": used, "active": active}})
        m = re.match(r"^/api/v1/shares/([^/]+)$", path)
        if m and ID_RE.match(m.group(1)):  # progress check for the sender
            member = self.auth()
            if not member:
                return self.reply(401, {"error": "bad key"})
            info = self.store.meta(m.group(1))
            if not self.own(member, info):
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
        counted = end == size - 1 and self.command != "HEAD"  # the final range is "a download"
        if counted and not self.store.reserve(sid):
            return self.reply(404, {"error": "expired"})
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
        try:
            with open(self.store.path(sid, "blob"), "rb") as f:
                f.seek(start)
                while left > 0:
                    chunk = f.read(min(65536, left))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)
        except OSError:
            left = -1
        if left != 0 and counted:  # the reader went away: it did not get the file, so it did not use the link up
            self.store.release(sid)

    def refuse(self, code, error, message, **extra):
        """Turn an upload away before its body is read, with a reason the sender's app can word."""
        self.reply(code, dict({"error": error, "message": message}, **extra))
        # Read (and drop) what the sender is still pushing, up to a point: a client that is cut off mid-upload
        # often reports "connection closed" and never shows the reason above.
        left = min(int(self.headers.get("Content-Length", "0") or 0), 64 * MB)
        try:
            while left > 0:
                chunk = self.rfile.read(min(65536, left))
                if not chunk:
                    break
                left -= len(chunk)
        except OSError:
            pass
        self.close_connection = True

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/api/v1/join":
            return self.join()
        if path != "/api/v1/shares":
            return self.reply(404, {"error": "not found"})
        member = self.auth()
        if not member:
            return self.reply(401, {"error": "bad key"})
        cfg, lim = self.server.cfg, self.server.members.limits(member)
        try:
            length = int(self.headers.get("Content-Length", ""))
            ttl = min(int(self.headers.get("X-TTL", "3600")), lim["max_ttl"])
            max_dl = max(0, int(self.headers.get("X-Max-Downloads", "0")))
        except ValueError:
            return self.reply(400, {"error": "bad headers"})
        if length <= 0 or ttl <= 0:
            return self.reply(400, {"error": "bad headers"})
        if length > lim["max_file"]:
            return self.refuse(413, "file_too_big", "This server accepts files up to %d MB" % (lim["max_file"] // MB), limit=lim["max_file"])
        if lim["max_active"] and self.store.active(member["id"]) >= lim["max_active"]:
            return self.refuse(429, "too_many_shares", "You can have %d shares at once on this server" % lim["max_active"], limit=lim["max_active"])
        used = self.store.used(member["id"])
        if used + length > lim["quota"]:
            return self.refuse(413, "quota", "Your space on this server is full (%d of %d MB used)" % (used // MB, lim["quota"] // MB),
                               limit=lim["quota"], used=used)
        if self.store.used() + length > cfg.max_total:
            return self.refuse(413, "server_full", "The server is full right now")
        pin = self.headers.get("X-Pin", "")
        salt = secrets.token_bytes(16)
        sid = secrets.token_urlsafe(16)
        while sid.startswith("-"):  # an id that looks like an option is a trap for the command line
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
            "size": length, "expires_at": expires, "max_downloads": max_dl, "downloads": 0, "owner": member["id"],
            "pin_salt": salt.hex() if pin else "",
            "pin_hash": hashlib.pbkdf2_hmac("sha256", pin.encode(), salt, 100_000).hex() if pin else "",
        })
        self.reply(201, {"id": sid, "expires_at": expires})

    def join(self):
        """Trade an invite token for the member's key (shown once)."""
        try:
            token = json.loads(self.rfile.read(min(int(self.headers.get("Content-Length", "0")), 4096)) or b"{}").get("token", "")
        except (ValueError, AttributeError):
            token = ""
        got = self.server.members.redeem(str(token), self.ip())
        if got == "locked":
            return self.reply(429, {"error": "too many tries"})
        if not got:
            return self.reply(404, {"error": "invalid_invite", "message": "This invitation is not valid any more"})
        self.reply(200, {"name": got[0], "key": got[1]})

    def do_DELETE(self):
        m = re.match(r"^/api/v1/shares/([^/]+)$", urlparse(self.path).path)
        if not m or not ID_RE.match(m.group(1)):
            return self.reply(404, {"error": "not found"})
        member = self.auth()
        if not member:
            return self.reply(401, {"error": "bad key"})
        if self.own(member, self.store.meta(m.group(1))):
            self.store.delete(m.group(1))
        self.reply(200, {"ok": True})


def make_server(cfg):
    os.makedirs(cfg.data_dir, exist_ok=True)
    srv = ThreadingHTTPServer((cfg.host, cfg.port), Handler)
    srv.cfg, srv.store, srv.members = cfg, Store(cfg), Members(cfg)
    srv.daemon_threads = True

    def cleaner():
        while True:
            time.sleep(60)
            srv.store.sweep()
    threading.Thread(target=cleaner, daemon=True).start()
    return srv


def cli(cfg, argv):
    """Host commands: relay.py members|invite|shares ... (run inside the container with docker compose exec)."""
    import argparse
    p = argparse.ArgumentParser(prog="relay.py", description="Notch relay: run it, or manage who may use it.")
    sub = p.add_subparsers(dest="cmd")
    sub.add_parser("serve", help="run the relay (the default)")

    def limits(sp):
        sp.add_argument("--quota-mb", type=int, help="disk space for their live shares")
        sp.add_argument("--max-file-mb", type=int, help="largest single upload")
        sp.add_argument("--max-ttl-hours", type=int, help="longest a link may live")
        sp.add_argument("--max-active", type=int, help="shares at the same time")
    mem = sub.add_parser("members", help="manage members").add_subparsers(dest="action", required=True)
    add = mem.add_parser("add", help="add a member and print their invitation")
    add.add_argument("name"); limits(add)
    mem.add_parser("list", help="members, limits and what they use")
    setp = mem.add_parser("set", help="change a member's limits")
    setp.add_argument("name"); limits(setp)
    rev = mem.add_parser("revoke", help="end a member's access and delete their shares")
    rev.add_argument("name")
    inv = sub.add_parser("invite", help="a new invitation for a member (also resets their key)")
    inv.add_argument("name")
    sh = sub.add_parser("shares", help="live shares").add_subparsers(dest="action", required=True)
    sh.add_parser("list", help="id, owner, size, expiry, downloads")
    kill = sh.add_parser("kill", help="delete one share now")
    kill.add_argument("id")
    args = p.parse_args(argv)
    if args.cmd in (None, "serve"):
        return False

    members, store = Members(cfg), Store(cfg)
    names = {OWNER: "owner"} | {m["id"]: m["name"] for m in members.all()}
    pick = lambda a: {k: getattr(a, k) for k in ("quota_mb", "max_file_mb", "max_ttl_hours", "max_active")}
    try:
        if args.cmd == "members" and args.action == "add":
            members.add(args.name, **pick(args))
            print("Added %s.\nInvitation (valid %d days, single use):\n  %s" % (args.name, INVITE_DAYS, members.invite_link(members.invite(args.name))))
        elif args.cmd == "members" and args.action == "set":
            if not members.get(args.name):
                raise ValueError("no member called %r" % args.name)
            members.update(args.name, **pick(args))
            print("Updated %s." % args.name)
        elif args.cmd == "members" and args.action == "revoke":
            m = members.get(args.name)
            if not m:
                raise ValueError("no member called %r" % args.name)
            members.revoke(args.name)
            gone = [sid for sid, meta in store.shares() if meta.get("owner", OWNER) == m["id"]]
            for sid in gone:
                store.delete(sid)
            print("Revoked %s; %d share(s) deleted." % (args.name, len(gone)))
        elif args.cmd == "invite":
            print("Invitation for %s (valid %d days, single use; using it gives them a new key):\n  %s"
                  % (args.name, INVITE_DAYS, members.invite_link(members.invite(args.name))))
        elif args.cmd == "members":  # list
            print("%-20s %-8s %-18s %-8s %-9s %s" % ("NAME", "STATE", "SPACE", "SHARES", "MAX FILE", "MAX LIFE"))
            for m in [{"id": OWNER, "name": "owner", "revoked": 0, "key_hash": "-"}] + members.all():
                lim = members.limits(m)
                state = "revoked" if m["revoked"] else ("active" if m["key_hash"] else "invited")
                print("%-20s %-8s %-18s %-8s %-9s %s" % (
                    m["name"], state, "%d / %d MB" % (store.used(m["id"]) // MB, lim["quota"] // MB),
                    "%d / %s" % (store.active(m["id"]), lim["max_active"] or "-"), "%d MB" % (lim["max_file"] // MB),
                    "%d h" % (lim["max_ttl"] // 3600)))
        elif args.cmd == "shares" and args.action == "list":
            for sid, m in store.shares():
                print("%s  %-14s %8.1f MB  expires in %5d min  downloads %d%s" % (
                    sid, names.get(m.get("owner", OWNER), "?"), m["size"] / MB, max(0, m["expires_at"] - time.time()) // 60,
                    m["downloads"], " (PIN)" if m["pin_hash"] else ""))
        elif args.cmd == "shares":
            if not ID_RE.match(args.id) or not store.meta(args.id):
                raise ValueError("no live share with that id")
            store.delete(args.id)
            print("Deleted.")
    except ValueError as e:
        sys.exit("Error: %s" % e)
    return True


if __name__ == "__main__":
    config = Config()
    if len(config.api_key) < 32:
        sys.exit("RELAY_API_KEY must be set to at least 32 characters (openssl rand -hex 32)")
    if cli(config, sys.argv[1:]):
        sys.exit(0)
    server = make_server(config)
    print("Notch relay listening on %s:%d" % (config.host, config.port), flush=True)
    server.serve_forever()
