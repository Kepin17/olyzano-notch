import base64
import contextlib
import http.client
import io
import json
import os
import re
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import cloud  # noqa: E402
import relay  # noqa: E402

HOST_KEY = "h" * 40
MB = 1024 * 1024
ACCESS, SECRET = "AKIATESTACCESS", "testsecretkey/with+chars"


class FakeS3(ThreadingHTTPServer):
    """An S3 that keeps objects in a dict and checks every request's Signature V4 with the same signer the relay uses."""

    def __init__(self):
        super().__init__(("127.0.0.1", 0), FakeS3.Handler)
        self.objects, self.bad_signatures, self.puts = {}, 0, 0
        self.fail_puts = False
        self.daemon_threads = True

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):
            pass

        def verify(self):
            auth = self.headers.get("Authorization", "")
            m = re.match(r"AWS4-HMAC-SHA256 Credential=([^/]+)/(\d{8})/([^/]+)/s3/aws4_request, SignedHeaders=([^,]+), Signature=([0-9a-f]{64})", auth)
            ok = bool(m) and m.group(1) == ACCESS
            if ok:
                names = m.group(4).split(";")
                extra = {n: self.headers[n] for n in names if n not in ("host", "x-amz-content-sha256", "x-amz-date")}
                when = time.strptime(self.headers["x-amz-date"], "%Y%m%dT%H%M%SZ")
                want = cloud.sigv4(self.command, self.headers["Host"], self.path, {}, extra, self.headers["x-amz-content-sha256"],
                                   ACCESS, SECRET, m.group(3), when)["authorization"]
                ok = want == auth
            if not ok:
                self.server.bad_signatures += 1
                self.send_response(403)
                self.send_header("Content-Length", "0")
                self.end_headers()
            return ok

        def key(self):
            return self.path.split("/", 2)[2]  # /bucket/<key>

        def do_PUT(self):
            n = int(self.headers.get("Content-Length", "0"))
            data = b""
            while len(data) < n:
                chunk = self.rfile.read(n - len(data))
                if not chunk:
                    return  # the sender went away: nothing is stored
                data += chunk
            if not self.verify():
                return
            self.server.puts += 1
            if self.server.fail_puts:
                self.send_response(500)
            else:
                self.server.objects[self.key()] = data
                self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()

        def do_GET(self):
            if not self.verify():
                return
            data = self.server.objects.get(self.key())
            if data is None:
                self.send_response(404)
                self.send_header("Content-Length", "0")
                return self.end_headers()
            rng = re.match(r"bytes=(\d+)-(\d*)$", self.headers.get("Range", ""))
            code, start, end = 200, 0, len(data) - 1
            if rng:
                start, end = int(rng.group(1)), int(rng.group(2)) if rng.group(2) else len(data) - 1
                code = 206
            self.send_response(code)
            self.send_header("Content-Length", str(end - start + 1))
            if code == 206:
                self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, len(data)))
            self.end_headers()
            self.wfile.write(data[start:end + 1])

        def do_HEAD(self):
            if self.verify():
                self.send_response(200 if self.key() in self.server.objects else 404)
                self.send_header("Content-Length", "0")
                self.end_headers()

        def do_DELETE(self):
            if self.verify():
                self.server.objects.pop(self.key(), None)
                self.send_response(204)
                self.end_headers()


def meta(text):
    return base64.b64encode(text.encode()).decode()


class SignerTest(unittest.TestCase):
    def test_matches_the_published_aws_example(self):
        # "GET Object" with a Range header, from the AWS Signature V4 documentation
        headers = cloud.sigv4("GET", "examplebucket.s3.amazonaws.com", "/test.txt", {}, {"range": "bytes=0-9"},
                              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                              "AKIAIOSFODNN7EXAMPLE", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", "us-east-1",
                              time.strptime("20130524T000000Z", "%Y%m%dT%H%M%SZ"))
        self.assertTrue(headers["authorization"].endswith("Signature=f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41"),
                        headers["authorization"])


class CloudTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.s3 = FakeS3()
        threading.Thread(target=self.s3.serve_forever, daemon=True).start()
        self.env = {"RELAY_API_KEY": HOST_KEY, "DATA_DIR": self.dir.name, "PORT": "0", "BIND": "127.0.0.1",
                    "MAX_FILE_MB": "5", "MAX_TOTAL_MB": "20", "MAX_TTL_HOURS": "24", "PUBLIC_URL": "https://share.test",
                    "S3_ENDPOINT": "http://127.0.0.1:%d" % self.s3.server_address[1], "S3_BUCKET": "bucket",
                    "S3_ACCESS_KEY": ACCESS, "S3_SECRET_KEY": SECRET, "CLOUD_MAX_TOTAL_MB": "8"}
        self.cfg = relay.Config(self.env)
        self.srv = relay.make_server(self.cfg)
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.s3.shutdown()
        self.s3.server_close()
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
        return r.status, data, r

    def jcall(self, method, path, obj=None, key=HOST_KEY):
        status, data, _ = self.call(method, path, None if obj is None else json.dumps(obj).encode(), key=key)
        return status, (json.loads(data) if data else {})

    def cli(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            relay.cli(self.cfg, list(argv))
        return out.getvalue()

    def join(self, name, **limits):
        args = ["members", "add", name] + [x for k, v in limits.items() for x in ("--" + k.replace("_", "-"), str(v))]
        link = self.cli(*args).strip().splitlines()[-1].strip()
        status, body, _ = self.call("POST", "/api/v1/join", json.dumps({"token": link.split("#", 1)[1]}).encode())
        self.assertEqual(status, 200, body)
        return json.loads(body)["key"]

    def put(self, key, size=1000, text="meta", expect=201, content=None):
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", content if content is not None else b"x" * size,
                                    {"X-Meta": meta(text)}, key)
        self.assertEqual(status, expect, body)
        return json.loads(body)

    # ---- the files ---------------------------------------------------------------------------
    def test_a_file_goes_up_comes_back_and_goes_away(self):
        alice, bob = self.join("alice", cloud_quota_mb=3), self.join("bob", cloud_quota_mb=3)
        got = self.put(alice, content=b"hello cloud" * 100, text="name=report.pdf")
        self.assertEqual(self.s3.bad_signatures, 0)
        self.assertEqual(len(self.s3.objects), 1)
        status, body, _ = self.call("GET", "/api/v1/cloud?since=0", key=bob)  # a second member sees it
        feed = json.loads(body)
        self.assertEqual([o["id"] for o in feed["objects"]], [got["id"]])
        self.assertEqual(base64.b64decode(feed["objects"][0]["meta"]), b"name=report.pdf")
        self.assertEqual(feed["objects"][0]["size"], 1100)
        status, data, _ = self.call("GET", "/api/v1/cloud/objects/" + got["id"], key=bob)
        self.assertEqual((status, data), (200, b"hello cloud" * 100))
        status, data, r = self.call("GET", "/api/v1/cloud/objects/" + got["id"], headers={"Range": "bytes=6-10"}, key=bob)
        self.assertEqual((status, data), (206, b"cloud"))
        self.assertEqual(r.getheader("Content-Range"), "bytes 6-10/1100")
        # only what changed since the last time
        self.assertEqual(json.loads(self.call("GET", "/api/v1/cloud?since=%d" % feed["seq"], key=bob)[1])["objects"], [])
        status, data, _ = self.call("POST", "/api/v1/cloud/objects/%s/meta" % got["id"], json.dumps({"meta": meta("name=new.pdf")}).encode(), key=alice)
        self.assertEqual(status, 200, data)
        changed = json.loads(self.call("GET", "/api/v1/cloud?since=%d" % feed["seq"], key=bob)[1])["objects"]
        self.assertEqual(base64.b64decode(changed[0]["meta"]), b"name=new.pdf")
        # delete: the contents leave the store at once, the row stays as a tombstone
        status, data, _ = self.call("DELETE", "/api/v1/cloud/objects/" + got["id"], key=alice)
        self.assertEqual(status, 200, data)
        self.assertEqual(self.s3.objects, {})
        gone = json.loads(self.call("GET", "/api/v1/cloud?since=0", key=bob)[1])["objects"][0]
        self.assertTrue(gone["deleted"])
        self.assertEqual(self.call("GET", "/api/v1/cloud/objects/" + got["id"], key=bob)[0], 404)

    def test_members_change_only_their_own_files_and_the_host_all(self):
        alice, bob = self.join("alice"), self.join("bob")
        got = self.put(alice)
        self.assertEqual(self.call("DELETE", "/api/v1/cloud/objects/" + got["id"], key=bob)[0], 404)
        self.assertEqual(self.call("POST", "/api/v1/cloud/objects/%s/meta" % got["id"], json.dumps({"meta": meta("x")}).encode(), key=bob)[0], 404)
        self.assertEqual(self.call("DELETE", "/api/v1/cloud/objects/" + got["id"], key=HOST_KEY)[0], 200)

    def test_unknown_keys_and_missing_metadata_are_turned_away(self):
        self.assertEqual(self.call("GET", "/api/v1/cloud?since=0", key="nope")[0], 401)
        alice = self.join("alice")
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", b"abc", {}, alice)  # no metadata
        self.assertEqual(status, 400)
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", b"abc", {"X-Meta": base64.b64encode(b"m" * (cloud.META_MAX + 1)).decode()}, alice)
        self.assertEqual(status, 400)
        self.assertEqual(self.s3.objects, {})

    def test_space_is_kept_per_member_and_for_the_whole_shelf(self):
        alice, bob = self.join("alice", cloud_quota_mb=2), self.join("bob", cloud_quota_mb=8)
        self.put(alice, MB + MB // 2)
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", b"x" * MB, {"X-Meta": meta("m")}, alice)
        self.assertEqual(status, 413)
        self.assertEqual(json.loads(body)["error"], "cloud_quota")
        self.put(bob, 4 * MB)  # the shelf holds 8 MB in all: 1.5 + 4 used
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", b"x" * (3 * MB), {"X-Meta": meta("m")}, bob)
        self.assertEqual(status, 413)
        self.assertEqual(json.loads(body)["error"], "server_full")
        ping = json.loads(self.call("GET", "/api/v1/ping", key=alice)[1])
        self.assertEqual(ping["cloud"], {"on": True, "used": MB + MB // 2, "quota": 2 * MB})

    def test_a_failed_store_keeps_nothing_and_frees_the_space(self):
        alice = self.join("alice", cloud_quota_mb=2)
        self.s3.fail_puts = True
        self.put(alice, MB, expect=502)
        self.s3.fail_puts = False
        self.assertEqual(self.s3.objects, {})
        self.put(alice, MB + MB // 2)  # the failed try did not use up the space
        # an upload that stops halfway
        s = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        s.putrequest("POST", "/api/v1/cloud/objects")
        s.putheader("Authorization", "Bearer " + alice)
        s.putheader("X-Meta", meta("m"))
        s.putheader("Content-Length", str(400 * 1024))
        s.endheaders()
        s.send(b"x" * 1000)
        s.close()
        time.sleep(0.5)
        self.assertEqual(len(self.s3.objects), 1)
        self.assertEqual(self.srv.cloud.reserved.get(self.srv.members.get("alice")["id"], 0), 0)

    def test_a_store_that_is_down_is_an_answer_not_a_crash(self):
        alice = self.join("alice", cloud_quota_mb=2)
        got = self.put(alice, 1000)
        self.s3.shutdown()
        self.s3.server_close()  # the storage server stops
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", b"x" * 1000, {"X-Meta": meta("m")}, alice)
        self.assertEqual(status, 502)
        info = json.loads(body)
        self.assertEqual(info["error"], "cloud_unavailable")
        self.assertIn("not reachable", info["message"])
        self.assertEqual(self.call("GET", "/api/v1/cloud/objects/" + got["id"], key=alice)[0], 502)
        status, body, _ = self.call("DELETE", "/api/v1/cloud/objects/" + got["id"], key=alice)
        self.assertEqual(status, 502)  # not deleted, so still counted and still listed
        self.assertEqual(len(json.loads(self.call("GET", "/api/v1/cloud?since=0", key=alice)[1])["objects"]), 1)
        self.assertEqual(self.call("GET", "/api/v1/ping", key=alice)[0], 200)  # and the relay itself carries on
        self.assertEqual(self.jcall("DELETE", "/api/v1/members/alice", {"reason": "x", "delete_files": True})[0], 200)  # even removing a member
        owner_view = self.jcall("GET", "/api/v1/cloud?since=0")[1]["objects"]
        self.assertEqual([o["owner"] for o in owner_view if not o["deleted"]], [0])  # what could not be deleted is the host's now

    def test_the_shelf_is_off_without_a_store(self):
        self.srv.cloud = None
        alice = self.join("alice")
        self.assertEqual(self.call("GET", "/api/v1/cloud?since=0", key=alice)[0], 404)
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", b"abc", {"X-Meta": meta("m")}, alice)
        self.assertEqual(status, 404)

    # ---- the host decides -------------------------------------------------------------------------------
    def test_a_paused_member_is_told_why_and_can_come_back(self):
        alice = self.join("alice")
        got = self.put(alice)
        status, body = self.jcall("POST", "/api/v1/members/alice/disable", {"reason": ""})
        self.assertEqual((status, body["error"]), (400, "reason_required"))
        self.assertEqual(self.jcall("POST", "/api/v1/members/alice/disable", {"reason": "over the agreed space"})[0], 200)
        status, body = self.jcall("GET", "/api/v1/cloud?since=0", key=alice)
        self.assertEqual((status, body["error"], body["reason"]), (403, "disabled", "over the agreed space"))
        self.assertIn("over the agreed space", body["message"])
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", b"abc", {"X-Meta": meta("m")}, alice)
        self.assertEqual((status, json.loads(body)["error"]), (403, "disabled"))
        status, body = self.jcall("GET", "/api/v1/ping", key=alice)  # the app can show it
        self.assertEqual((status, body["ok"], body["disabled"], body["reason"]), (200, False, True, "over the agreed space"))
        self.assertEqual(self.call("POST", "/api/v1/shares", b"x" * 10, {}, alice)[0], 403)  # the shares are paused too
        listing = self.jcall("GET", "/api/v1/members")[1]["members"][0]
        self.assertEqual((listing["disabled"], listing["disabled_reason"]), (True, "over the agreed space"))
        self.assertEqual(self.jcall("GET", "/api/v1/cloud?since=0")[0], 200)  # their file is still there
        self.assertEqual(self.jcall("POST", "/api/v1/members/alice/enable")[0], 200)
        self.assertEqual(self.call("GET", "/api/v1/cloud/objects/" + got["id"], key=alice)[0], 200)

    def test_a_removed_member_is_told_why_and_their_files_go_or_stay(self):
        alice, bob = self.join("alice"), self.join("bob")
        keep, lose = self.put(alice), self.put(bob)
        self.assertEqual(self.jcall("DELETE", "/api/v1/members/alice", {"reason": "left the team", "delete_files": False})[0], 200)
        status, body = self.jcall("GET", "/api/v1/cloud?since=0", key=alice)
        self.assertEqual((status, body["error"], body["reason"]), (401, "removed", "left the team"))
        self.assertEqual(self.call("GET", "/api/v1/cloud/objects/" + keep["id"], key=HOST_KEY)[0], 200)  # now the host's
        owner_feed = {o["id"]: o["owner"] for o in self.jcall("GET", "/api/v1/cloud?since=0")[1]["objects"]}
        self.assertEqual(owner_feed[keep["id"]], relay.OWNER)
        self.assertEqual(self.jcall("DELETE", "/api/v1/members/bob", {"reason": "spam", "delete_files": True})[0], 200)
        self.assertEqual(self.call("GET", "/api/v1/cloud/objects/" + lose["id"], key=HOST_KEY)[0], 404)
        self.assertEqual(len(self.s3.objects), 1)  # only the host's now
        self.assertEqual(self.jcall("GET", "/api/v1/members")[1]["members"], [])
        self.assertEqual(self.call("GET", "/api/v1/ping", key="not-a-key")[0], 401)

    def test_the_host_sets_the_space_and_reads_what_is_used(self):
        alice = self.join("alice", cloud_quota_mb=2)
        self.put(alice, MB)
        row = self.jcall("GET", "/api/v1/members")[1]["members"][0]
        self.assertEqual(row["cloud"], {"on": True, "used": MB, "quota": 2 * MB, "files": 1})
        status, body = self.jcall("POST", "/api/v1/members/alice/limits", {"cloud_quota_mb": 1, "quota_mb": 3})
        self.assertEqual((status, body["limits"]["cloud_quota"], body["limits"]["quota"]), (200, MB, 3 * MB))
        status, body, _ = self.call("POST", "/api/v1/cloud/objects", b"x" * 10, {"X-Meta": meta("m")}, alice)  # at its limit now
        self.assertEqual((status, json.loads(body)["error"]), (413, "cloud_quota"))
        self.assertEqual(self.jcall("POST", "/api/v1/members/alice/limits", {"cloud_quota_mb": 4})[0], 200)
        self.put(alice, 10)
        self.assertEqual(self.jcall("POST", "/api/v1/members/alice/limits", {"cloud_quota_mb": "lots"})[0], 400)
        self.assertEqual(self.call("POST", "/api/v1/members/alice/limits", b"{}", key=alice)[0], 403)  # members cannot

    def test_cli_pauses_and_removes(self):
        self.join("alice")
        self.assertIn("Paused alice", self.cli("members", "disable", "alice", "--reason", "invoice"))
        self.assertIn("paused", self.cli("members", "list"))
        self.assertIn("may use", self.cli("members", "enable", "alice"))
        self.assertIn("Removed alice", self.cli("members", "remove", "alice", "--reason", "bye"))


if __name__ == "__main__":
    unittest.main()
