import contextlib
import http.client
import io
import json
import os
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import relay  # noqa: E402

HOST_KEY = "h" * 40
MB = 1024 * 1024


class TeamTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.env = {"RELAY_API_KEY": HOST_KEY, "DATA_DIR": self.dir.name, "PORT": "0", "BIND": "127.0.0.1",
                    "MAX_FILE_MB": "5", "MAX_TOTAL_MB": "20", "MAX_TTL_HOURS": "24", "PUBLIC_URL": "https://share.test"}
        self.cfg = relay.Config(self.env)
        self.srv = relay.make_server(self.cfg)
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.dir.cleanup()

    def call(self, method, path, body=None, headers=None, key=None):
        h = dict(headers or {})
        if key:
            h["Authorization"] = "Bearer " + key
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request(method, path, body=body, headers=h)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, data

    def cli(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            relay.cli(self.cfg, list(argv))
        return out.getvalue()

    def join(self, name, **limits):
        """Add a member the way the host does, redeem the invitation the way Notch does; -> their key."""
        args = ["members", "add", name] + [x for k, v in limits.items() for x in ("--" + k.replace("_", "-"), str(v))]
        link = self.cli(*args).strip().splitlines()[-1].strip()
        self.assertTrue(link.startswith("https://share.test/join#"), link)
        status, body = self.call("POST", "/api/v1/join", json.dumps({"token": link.split("#", 1)[1]}).encode())
        self.assertEqual(status, 200, body)
        self.assertEqual(json.loads(body)["name"], name)
        return json.loads(body)["key"], link

    def upload(self, key, size=1000, expect=201, **headers):
        status, body = self.call("POST", "/api/v1/shares", b"x" * size, headers, key)
        self.assertEqual(status, expect, body)
        return json.loads(body)

    # ---- joining ----------------------------------------------------------------------------
    def test_invitation_is_single_use_and_gives_a_working_key(self):
        key, link = self.join("alice")
        self.assertEqual(len(key), 64)
        status, body = self.call("GET", "/api/v1/ping", key=key)
        self.assertEqual((status, json.loads(body)["member"]), (200, "alice"))
        self.assertEqual(self.call("POST", "/api/v1/join", json.dumps({"token": link.split("#", 1)[1]}).encode())[0], 404)
        self.assertEqual(self.call("GET", "/api/v1/ping", key="not-a-key")[0], 401)

    def raw(self, data, read_all=True):
        """Send raw bytes on one connection; -> everything the server wrote until it closed (or stopped for a moment)."""
        import socket
        sk = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        sk.sendall(data)
        out = b""
        try:
            while True:
                chunk = sk.recv(65536)
                if not chunk:
                    break
                out += chunk
        except socket.timeout:
            pass
        sk.close()
        return out

    def test_unread_body_cannot_smuggle_a_second_request(self):
        # a 401 is sent before the body is read; the body holds a full request that must not be run as the next one
        inner = b"GET /api/v1/ping HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + HOST_KEY.encode() + b"\r\n\r\n"
        outer = (b"POST /api/v1/shares HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\n\r\n" % len(inner)) + inner
        out = self.raw(outer)
        self.assertEqual(out.count(b"HTTP/1.1 "), 1, out)
        self.assertIn(b"401", out.split(b"\r\n")[0])

    def test_ambiguous_framing_is_refused(self):
        out = self.raw(b"POST /api/v1/join HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 4\r\n\r\n0\r\n\r\n")
        self.assertIn(b"400", out.split(b"\r\n")[0])
        out = self.raw(b"POST /api/v1/join HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\nContent-Length: 40\r\n\r\n{}  ")
        self.assertIn(b"400", out.split(b"\r\n")[0])

    def test_one_invitation_gives_one_key_even_when_used_in_parallel(self):
        link = self.cli("members", "add", "racer").strip().splitlines()[-1].strip()
        token = link.split("#", 1)[1]
        results = []
        def go():
            results.append(self.call("POST", "/api/v1/join", json.dumps({"token": token}).encode())[0])
        ts = [threading.Thread(target=go) for _ in range(8)]
        [t.start() for t in ts]
        [t.join() for t in ts]
        self.assertEqual(results.count(200), 1, results)

    def test_parallel_uploads_cannot_pass_the_space_quota(self):
        key, _ = self.join("sam", quota_mb=3)
        results = []
        def go():
            results.append(self.call("POST", "/api/v1/shares", b"x" * (2 * MB), {"X-TTL": "3600"}, key)[0])
        ts = [threading.Thread(target=go) for _ in range(6)]
        [t.start() for t in ts]
        [t.join() for t in ts]
        self.assertEqual(results.count(201), 1, results)

    def test_a_changing_forwarded_address_does_not_reset_the_pin_lock(self):
        self.srv.cfg.trust_proxy = True
        status, body = self.call("POST", "/api/v1/shares", b"secret", {"X-TTL": "3600", "X-Pin": "1234"}, HOST_KEY)
        sid = json.loads(body)["id"]
        for i in range(relay.PIN_MAX_FAILS * 6 + 1):
            self.call("GET", "/s/%s/blob" % sid, headers={"X-Pin": "0000", "X-Forwarded-For": "6.6.6.%d, 10.0.0.%d" % (i, i)})
        status, _ = self.call("GET", "/s/%s/blob" % sid, headers={"X-Pin": "1234", "X-Forwarded-For": "7.7.7.7, 10.9.9.9"})
        self.assertEqual(status, 429)

    def test_expired_and_wrong_invitations_fail_and_guessing_is_limited(self):
        link = self.cli("members", "add", "bob").strip().splitlines()[-1].strip()
        token = link.split("#", 1)[1]
        with self.srv.members.db() as c:
            c.execute("UPDATE invites SET expires_at = 1")
        self.assertEqual(self.call("POST", "/api/v1/join", json.dumps({"token": token}).encode())[0], 404)
        codes = [self.call("POST", "/api/v1/join", json.dumps({"token": "guess%d" % i}).encode())[0] for i in range(12)]
        self.assertEqual(codes[-1], 429)  # locked out after 10 wrong tries

    def test_reissuing_an_invitation_resets_the_key(self):
        old, _ = self.join("carol")
        link = self.cli("invite", "carol").strip().splitlines()[-1].strip()
        status, body = self.call("POST", "/api/v1/join", json.dumps({"token": link.split("#", 1)[1]}).encode())
        new = json.loads(body)["key"]
        self.assertEqual(status, 200)
        self.assertEqual(self.call("GET", "/api/v1/ping", key=old)[0], 401)
        self.assertEqual(self.call("GET", "/api/v1/ping", key=new)[0], 200)

    def test_join_page_is_served(self):
        status, body = self.call("GET", "/join")
        self.assertEqual(status, 200)
        self.assertIn(b"Join a server", body)

    # ---- limits -----------------------------------------------------------------------------
    def test_member_limits(self):
        key, _ = self.join("dana", quota_mb=1, max_file_mb=1, max_ttl_hours=1, max_active=2)
        _, body = self.call("GET", "/api/v1/ping", key=key)
        lim = json.loads(body)["limits"]
        self.assertEqual((lim["quota"], lim["max_file"], lim["max_ttl"], lim["max_active"]), (MB, MB, 3600, 2))
        # ttl is clamped, not refused
        share = self.upload(key, 400_000, **{"X-TTL": "999999"})
        self.assertLessEqual(share["expires_at"], time.time() + 3601)
        # file too big for this member (the server itself would take 5 MB)
        status, body = self.call("POST", "/api/v1/shares", b"x" * (MB + 1), {}, key)
        self.assertEqual((status, json.loads(body)["error"]), (413, "file_too_big"))
        # space: 400k used, a second 400k fits, a third does not
        self.upload(key, 400_000)
        status, body = self.call("POST", "/api/v1/shares", b"x" * 400_000, {}, key)
        self.assertEqual(status, 429)  # two shares at once is the cap, checked first
        self.assertEqual(json.loads(body)["error"], "too_many_shares")
        usage = json.loads(self.call("GET", "/api/v1/ping", key=key)[1])["usage"]
        self.assertEqual((usage["used"], usage["active"]), (800_000, 2))

    def test_space_quota(self):
        key, _ = self.join("erin", quota_mb=1)
        self.upload(key, 700_000)
        status, body = self.call("POST", "/api/v1/shares", b"x" * 500_000, {}, key)
        self.assertEqual((status, json.loads(body)["error"]), (413, "quota"))
        # another member is unaffected
        other, _ = self.join("frank", quota_mb=1)
        self.upload(other, 700_000)

    def test_host_key_still_works_with_the_server_limits(self):
        self.upload(HOST_KEY, 4 * MB)
        status, body = self.call("POST", "/api/v1/shares", b"x" * (6 * MB), {}, HOST_KEY)
        self.assertEqual((status, json.loads(body)["error"]), (413, "file_too_big"))
        self.assertEqual(json.loads(self.call("GET", "/api/v1/ping", key=HOST_KEY)[1])["member"], "owner")

    # ---- isolation and revoking -------------------------------------------------------------
    def test_members_only_see_their_own_shares(self):
        a, _ = self.join("gina")
        b, _ = self.join("hugo")
        sid = self.upload(a)["id"]
        self.assertEqual(self.call("GET", "/api/v1/shares/" + sid, key=a)[0], 200)
        self.assertEqual(self.call("GET", "/api/v1/shares/" + sid, key=b)[0], 404)
        self.call("DELETE", "/api/v1/shares/" + sid, key=b)  # no effect
        self.assertEqual(self.call("GET", "/api/v1/shares/" + sid, key=a)[0], 200)
        self.assertEqual(self.call("GET", "/api/v1/shares/" + sid, key=HOST_KEY)[0], 200)  # the host sees all
        self.call("DELETE", "/api/v1/shares/" + sid, key=a)
        self.assertEqual(self.call("GET", "/api/v1/shares/" + sid, key=a)[0], 404)
        # the download link itself needs no key
        sid2 = self.upload(a)["id"]
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid2)[0], 200)

    def test_revoking_ends_access_and_deletes_their_shares(self):
        key, _ = self.join("ivan")
        keep, _ = self.join("judy")
        mine = self.upload(key)["id"]
        theirs = self.upload(keep)["id"]
        self.assertIn("1 share(s) deleted", self.cli("members", "revoke", "ivan"))
        self.assertEqual(self.call("GET", "/api/v1/ping", key=key)[0], 401)
        self.assertEqual(self.call("GET", "/s/%s/meta" % mine)[0], 404)
        self.assertEqual(self.call("GET", "/s/%s/meta" % theirs)[0], 200)

    # ---- command line -----------------------------------------------------------------------
    def test_host_manages_members_from_the_app(self):
        j = lambda b: json.dumps(b).encode()
        self.assertTrue(json.loads(self.call("GET", "/api/v1/ping", key=HOST_KEY)[1])["owner"])
        status, body = self.call("POST", "/api/v1/members", j({"name": "alice", "quota_mb": 3}), key=HOST_KEY)
        self.assertEqual(status, 201, body)
        link = json.loads(body)["link"]
        self.assertTrue(link.startswith("https://share.test/join#"))
        self.assertEqual(self.call("POST", "/api/v1/members", j({"name": "alice"}), key=HOST_KEY)[0], 409)
        self.assertEqual(self.call("POST", "/api/v1/members", j({"name": "/x"}), key=HOST_KEY)[0], 400)
        status, body = self.call("POST", "/api/v1/join", j({"token": link.split("#", 1)[1]}))
        key = json.loads(body)["key"]
        ping = json.loads(self.call("GET", "/api/v1/ping", key=key)[1])
        self.assertFalse(ping["owner"])
        self.assertEqual(self.call("GET", "/api/v1/members", key=key)[0], 403)
        self.assertEqual(self.call("POST", "/api/v1/members", j({"name": "bob"}), key=key)[0], 403)
        self.assertEqual(self.call("GET", "/api/v1/members")[0], 401)
        listed = json.loads(self.call("GET", "/api/v1/members", key=HOST_KEY)[1])["members"]
        self.assertEqual([(m["name"], m["joined"]) for m in listed], [("alice", True)])
        for i in range(4):
            self.call("POST", "/api/v1/members", j({"name": "m%d" % i}), key=HOST_KEY)
        page = json.loads(self.call("GET", "/api/v1/members?offset=1&limit=2", key=HOST_KEY)[1])
        self.assertEqual((page["total"], page["offset"], [m["name"] for m in page["members"]]), (5, 1, ["m2", "m1"]))  # newest first
        status, body = self.call("POST", "/api/v1/members/alice/invite", key=HOST_KEY)
        self.assertEqual(status, 200)
        self.assertIn("/join#", json.loads(body)["link"])
        self.assertEqual(self.call("DELETE", "/api/v1/members/alice", key=HOST_KEY)[0], 200)
        self.assertEqual(self.call("GET", "/api/v1/ping", key=key)[0], 401)
        self.assertEqual(self.call("DELETE", "/api/v1/members/nobody", key=HOST_KEY)[0], 404)

    def test_access_can_end_by_itself_and_be_revoked(self):
        j = lambda b: json.dumps(b).encode()
        status, body = self.call("POST", "/api/v1/members", j({"name": "temp", "access_hours": 1}), key=HOST_KEY)
        key = json.loads(self.call("POST", "/api/v1/join", j({"token": json.loads(body)["link"].split("#", 1)[1]}))[1])["key"]
        self.assertEqual(self.call("GET", "/api/v1/ping", key=key)[0], 200)
        listed = json.loads(self.call("GET", "/api/v1/members", key=HOST_KEY)[1])["members"][0]
        self.assertEqual(listed["access_hours"], 1)
        self.assertGreater(listed["expires_at"], time.time())
        with self.srv.members.db() as c:  # the hour is over
            c.execute("UPDATE members SET expires_at = ? WHERE name = 'temp'", (int(time.time()) - 1,))
        self.assertEqual(self.call("GET", "/api/v1/ping", key=key)[0], 401)
        body = self.call("POST", "/api/v1/members", j({"name": "forever"}), key=HOST_KEY)[1]
        key2 = json.loads(self.call("POST", "/api/v1/join", j({"token": json.loads(body)["link"].split("#", 1)[1]}))[1])["key"]
        self.assertEqual(self.call("GET", "/api/v1/ping", key=key2)[0], 200)
        self.assertEqual(self.call("DELETE", "/api/v1/members/forever", key=HOST_KEY)[0], 200)
        self.assertEqual(self.call("GET", "/api/v1/ping", key=key2)[0], 401)

    def test_member_space_is_set_at_creation_and_capped_at_5gb(self):
        j = lambda b: json.dumps(b).encode()
        for name, asked, got in (("small", 512, 512 * MB), ("huge", 99999, 5120 * MB)):
            self.call("POST", "/api/v1/members", j({"name": name, "quota_mb": asked}), key=HOST_KEY)
            listed = {m["name"]: m for m in json.loads(self.call("GET", "/api/v1/members", key=HOST_KEY)[1])["members"]}
            self.assertEqual(listed[name]["limits"]["quota"], min(got, self.cfg.max_total))

    def test_cli_lists_sets_and_kills(self):
        key, _ = self.join("kim", quota_mb=3)
        sid = self.upload(key)["id"]
        listing = self.cli("members", "list")
        self.assertIn("kim", listing)
        self.assertIn("active", listing)
        self.assertIn("owner", listing)
        self.assertIn("/ 3 MB", listing)
        self.cli("members", "set", "kim", "--quota-mb", "9", "--max-active", "4")
        self.assertIn("/ 9 MB", self.cli("members", "list"))
        self.assertIn(sid, self.cli("shares", "list"))
        self.assertIn("kim", self.cli("shares", "list"))
        self.cli("shares", "kill", sid)
        self.assertNotIn(sid, self.cli("shares", "list"))

    def test_cli_rejects_bad_input(self):
        self.cli("members", "add", "lee")
        with self.assertRaises(SystemExit):
            self.cli("members", "add", "lee")      # duplicate
        with self.assertRaises(SystemExit):
            self.cli("members", "add", "../etc")   # not a plain name
        with self.assertRaises(SystemExit):
            self.cli("invite", "nobody")
        with self.assertRaises(SystemExit):
            self.cli("shares", "kill", "short")


if __name__ == "__main__":
    unittest.main()
