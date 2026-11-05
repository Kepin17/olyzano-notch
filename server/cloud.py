"""The relay's cloud shelf: encrypted files kept in an S3-compatible store (MinIO, ...) behind the relay.

Standard library only. The members never hold the store's key: they talk to the relay with their own key, and the relay
signs (AWS Signature V4) and streams. The relay does not see names or contents either: the app encrypts both, and the
metadata (a few hundred bytes of ciphertext) lives in the relay's SQLite next to the members.
"""
import base64
import hashlib
import hmac
import http.client
import re
import secrets
import sqlite3
import threading
import time
from urllib.parse import quote, urlparse

MB = 1024 * 1024
META_MAX = 16 * 1024  # ciphertext of the name, type, category... of one file
UNSIGNED = "UNSIGNED-PAYLOAD"
CLOUD_ID_RE = re.compile(r"^[A-Za-z0-9_-]{16,64}$")


def _hmac(key, text):
    return hmac.new(key, text.encode(), hashlib.sha256).digest()


def sigv4(method, host, path, query, extra_headers, payload_hash, access, secret, region, when=None, service="s3"):
    """AWS Signature V4 for one request -> the headers to send (Authorization, x-amz-date, x-amz-content-sha256, host).
    `extra_headers` are signed too (lower-case names), `query` is a dict. Path-style, no double encoding (S3)."""
    when = when or time.gmtime()
    amz_date = time.strftime("%Y%m%dT%H%M%SZ", when)
    day = amz_date[:8]
    headers = {k.lower(): str(v).strip() for k, v in extra_headers.items()}
    headers.update({"host": host, "x-amz-content-sha256": payload_hash, "x-amz-date": amz_date})
    names = sorted(headers)
    canonical_query = "&".join("%s=%s" % (quote(str(k), safe="-_.~"), quote(str(v), safe="-_.~")) for k, v in sorted(query.items()))
    canonical = "\n".join([method, quote(path, safe="/-_.~"), canonical_query,
                           "".join("%s:%s\n" % (n, headers[n]) for n in names), ";".join(names), payload_hash])
    scope = "%s/%s/%s/aws4_request" % (day, region, service)
    to_sign = "\n".join(["AWS4-HMAC-SHA256", amz_date, scope, hashlib.sha256(canonical.encode()).hexdigest()])
    key = _hmac(_hmac(_hmac(_hmac(("AWS4" + secret).encode(), day), region), service), "aws4_request")
    signature = hmac.new(key, to_sign.encode(), hashlib.sha256).hexdigest()
    headers["authorization"] = "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s" % (access, scope, ";".join(names), signature)
    return headers


class S3Error(Exception):
    def __init__(self, status, message=""):
        super().__init__("S3 answered %s %s" % (status, message))
        self.status = status


class S3:
    """The few calls the cloud shelf needs, path-style: PUT (streamed), GET (streamed, ranges), HEAD, DELETE."""

    def __init__(self, endpoint, bucket, access, secret, region="us-east-1", prefix="cloud/"):
        url = urlparse(endpoint)
        self.https = url.scheme == "https"
        self.host = url.netloc
        self.base = url.path.rstrip("/")
        self.bucket, self.access, self.secret, self.region = bucket, access, secret, region
        self.prefix = prefix.strip("/") + "/" if prefix.strip("/") else ""

    def key(self, oid):
        return self.prefix + oid

    def _path(self, key):
        return "%s/%s/%s" % (self.base, self.bucket, key)

    def _conn(self):
        cls = http.client.HTTPSConnection if self.https else http.client.HTTPConnection
        return cls(self.host, timeout=60)

    def _request(self, method, key, headers=None, length=None):
        path = self._path(key)
        signed = sigv4(method, self.host, path, {}, headers or {}, UNSIGNED, self.access, self.secret, self.region)
        conn = self._conn()
        try:
            conn.putrequest(method, path, skip_host=True, skip_accept_encoding=True)
            for k, v in signed.items():
                conn.putheader(k, v)
            if length is not None:
                conn.putheader("Content-Length", str(length))
            conn.endheaders()
        except OSError as e:  # the store is not running, or not where S3_ENDPOINT says
            conn.close()
            raise S3Error(0, "the storage server is not reachable (%s)" % e)
        return conn

    def put(self, key, src, length, chunk=65536):
        """Stream `length` bytes from `src` (a file-like object) to the store. Raises S3Error or OSError (the sender went away)."""
        conn = self._request("PUT", key, length=length)
        try:
            left = length
            while left > 0:
                data = src.read(min(chunk, left))
                if not data:
                    raise OSError("short upload")
                conn.send(data)
                left -= len(data)
            resp = conn.getresponse()
            resp.read()
            if resp.status not in (200, 201, 204):
                raise S3Error(resp.status)
        finally:
            conn.close()

    def get(self, key, rng=None):
        """-> (status, headers, response, conn); the caller reads the response and closes the conn. Raises S3Error for 404 etc."""
        conn = self._request("GET", key, {"range": rng} if rng else None)
        try:
            resp = conn.getresponse()
        except OSError as e:
            conn.close()
            raise S3Error(0, "the storage server did not answer (%s)" % e)
        if resp.status not in (200, 206):
            resp.read()
            conn.close()
            raise S3Error(resp.status)
        return resp.status, resp, conn

    def head(self, key):
        conn = self._request("HEAD", key)
        try:
            resp = conn.getresponse()
            resp.read()
            return resp.status
        finally:
            conn.close()

    def delete(self, key):
        """True when the object is gone (or was never there); False when the store could not be asked or refused."""
        try:
            conn = self._request("DELETE", key)
        except S3Error:
            return False
        try:
            resp = conn.getresponse()
            resp.read()
            return resp.status in (200, 204, 404)
        except OSError:
            return False
        finally:
            conn.close()


class CloudStore:
    """Which files the cloud shelf holds (rows in the relay's database) and who owns them; the contents are in the store.
    `seq` grows with every change so an app asks only for what changed since the last time."""

    def __init__(self, db_path, s3, max_total):
        self.path, self.s3, self.max_total = db_path, s3, max_total
        self.lock = threading.Lock()
        self.reserved = {}  # owner -> bytes being uploaded, counted against the space like stored ones
        with self.db() as c:
            c.execute("""CREATE TABLE IF NOT EXISTS cloud_objects(
                id TEXT PRIMARY KEY, owner INTEGER NOT NULL, size INTEGER NOT NULL, meta BLOB NOT NULL,
                created INTEGER NOT NULL, updated INTEGER NOT NULL, deleted INTEGER NOT NULL DEFAULT 0, seq INTEGER NOT NULL)""")
            c.execute("CREATE INDEX IF NOT EXISTS cloud_seq ON cloud_objects(seq)")

    def db(self):
        c = sqlite3.connect(self.path, timeout=10)
        c.row_factory = sqlite3.Row
        return c

    def _next_seq(self, c):
        return (c.execute("SELECT COALESCE(MAX(seq), 0) FROM cloud_objects").fetchone()[0]) + 1

    def used(self, owner=None):
        with self.db() as c:
            if owner is None:
                row = c.execute("SELECT COALESCE(SUM(size), 0) FROM cloud_objects WHERE deleted = 0").fetchone()
            else:
                row = c.execute("SELECT COALESCE(SUM(size), 0) FROM cloud_objects WHERE deleted = 0 AND owner = ?", (owner,)).fetchone()
        return row[0]

    def count(self, owner):
        with self.db() as c:
            return c.execute("SELECT COUNT(*) FROM cloud_objects WHERE deleted = 0 AND owner = ?", (owner,)).fetchone()[0]

    def claim(self, owner, length, quota):
        """Room for `length` more bytes? -> None (reserved), or ("quota", used) / ("server_full", None)."""
        with self.lock:
            mine = self.used(owner) + self.reserved.get(owner, 0)
            if mine + length > quota:
                return ("quota", mine)
            if self.used() + sum(self.reserved.values()) + length > self.max_total:
                return ("server_full", None)
            self.reserved[owner] = self.reserved.get(owner, 0) + length
        return None

    def unclaim(self, owner, length):
        with self.lock:
            self.reserved[owner] = max(0, self.reserved.get(owner, 0) - length)

    def add(self, oid, owner, size, meta):
        now = int(time.time())
        with self.lock, self.db() as c:
            seq = self._next_seq(c)
            c.execute("INSERT INTO cloud_objects(id, owner, size, meta, created, updated, deleted, seq) VALUES(?,?,?,?,?,?,0,?)",
                      (oid, owner, size, meta, now, now, seq))
        return seq

    def get(self, oid):
        with self.db() as c:
            row = c.execute("SELECT * FROM cloud_objects WHERE id = ?", (oid,)).fetchone()
        return dict(row) if row else None

    def set_meta(self, oid, meta):
        with self.lock, self.db() as c:
            seq = self._next_seq(c)
            c.execute("UPDATE cloud_objects SET meta = ?, updated = ?, seq = ? WHERE id = ? AND deleted = 0", (meta, int(time.time()), seq, oid))
        return seq

    def tombstone(self, oid):
        """The file is gone: the contents are deleted from the store at once, the row stays (empty) so other apps learn of it."""
        if not self.s3.delete(self.s3.key(oid)):  # not deleted: keep the row, or the contents would sit there uncounted
            raise S3Error(0, "the storage server did not delete the file")
        with self.lock, self.db() as c:
            seq = self._next_seq(c)
            c.execute("UPDATE cloud_objects SET deleted = 1, meta = ?, size = 0, updated = ?, seq = ? WHERE id = ?",
                      (b"", int(time.time()), seq, oid))
        return seq

    def changes(self, since, limit=500):
        with self.db() as c:
            rows = c.execute("SELECT * FROM cloud_objects WHERE seq > ? ORDER BY seq LIMIT ?", (since, limit)).fetchall()
            top = c.execute("SELECT COALESCE(MAX(seq), 0) FROM cloud_objects").fetchone()[0]
        out = [{"id": r["id"], "owner": r["owner"], "size": r["size"], "deleted": bool(r["deleted"]), "updated": r["updated"],
                "seq": r["seq"], "meta": base64.b64encode(r["meta"]).decode()} for r in rows]
        return out, (out[-1]["seq"] if len(out) == limit else top)

    def owned(self, owner):
        with self.db() as c:
            return [r["id"] for r in c.execute("SELECT id FROM cloud_objects WHERE owner = ? AND deleted = 0", (owner,))]

    def purge_owner(self, owner):
        """Delete all of one member's files; those the store would not let go stay, owned by the host (OWNER = 0), to be deleted later."""
        for oid in self.owned(owner):
            try:
                self.tombstone(oid)
            except S3Error:
                pass
        self.transfer(owner, 0)

    def transfer(self, owner, to):
        with self.lock, self.db() as c:
            for r in c.execute("SELECT id FROM cloud_objects WHERE owner = ? AND deleted = 0", (owner,)).fetchall():
                c.execute("UPDATE cloud_objects SET owner = ?, seq = ? WHERE id = ?", (to, self._next_seq(c), r["id"]))

    @staticmethod
    def new_id():
        oid = secrets.token_urlsafe(16)
        while oid.startswith("-"):
            oid = secrets.token_urlsafe(16)
        return oid
