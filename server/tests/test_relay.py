import http.client
import os
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import relay  # noqa: E402

KEY = "k" * 40


class RelayTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        env = {"RELAY_API_KEY": KEY, "DATA_DIR": self.dir.name, "PORT": "0", "BIND": "127.0.0.1",
               "MAX_FILE_MB": "1", "MAX_TOTAL_MB": "2", "MAX_TTL_HOURS": "1"}
        self.srv = relay.make_server(relay.Config(env))
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.dir.cleanup()

    def call(self, method, path, body=None, headers=None, key=KEY):
        h = dict(headers or {})
        if key:
            h["Authorization"] = "Bearer " + key
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request(method, path, body=body, headers=h)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, data, r

    def upload(self, data=b"x" * 1000, **headers):
        status, body, _ = self.call("POST", "/api/v1/shares", data, headers)
        self.assertEqual(status, 201, body)
        import json
        return json.loads(body)["id"]

    def test_auth_required(self):
        self.assertEqual(self.call("POST", "/api/v1/shares", b"abc", key=None)[0], 401)
        self.assertEqual(self.call("POST", "/api/v1/shares", b"abc", key="wrong")[0], 401)
        self.assertEqual(self.call("GET", "/api/v1/ping")[0], 200)
        self.assertEqual(self.call("GET", "/api/v1/ping", key="no")[0], 401)

    def test_upload_download_and_page(self):
        sid = self.upload(b"hello world", **{"X-TTL": "60"})
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, key=None)[1], b"hello world")
        status, page, r = self.call("GET", "/s/%s" % sid, key=None)
        self.assertEqual(status, 200)
        self.assertIn(b"NSH1", page)
        self.assertIn(b"share-data", page)
        self.assertIn("script-src", r.getheader("Content-Security-Policy"))
        self.assertEqual(r.getheader("Cache-Control"), "no-store")

    def test_meta_has_clock_and_single_use(self):
        import json
        sid = self.upload(**{"X-Max-Downloads": "1"})
        meta = json.loads(self.call("GET", "/s/%s/meta" % sid, key=None)[1])
        self.assertTrue(meta["single_use"])
        self.assertLess(abs(meta["now"] - time.time()), 3)
        meta = json.loads(self.call("GET", "/s/%s/meta" % self.upload(), key=None)[1])
        self.assertFalse(meta["single_use"])

    def test_range(self):
        sid = self.upload(bytes(range(100)))
        status, data, r = self.call("GET", "/s/%s/blob" % sid, headers={"Range": "bytes=10-19"}, key=None)
        self.assertEqual((status, data), (206, bytes(range(10, 20))))
        self.assertEqual(r.getheader("Content-Range"), "bytes 10-19/100")
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, headers={"Range": "bytes=-5"}, key=None)[1],
                         bytes(range(95, 100)))
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, headers={"Range": "bytes=200-"}, key=None)[0], 416)

    def test_max_downloads(self):
        sid = self.upload(**{"X-Max-Downloads": "1"})
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, key=None)[0], 200)
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, key=None)[0], 404)
        self.assertFalse(os.path.exists(self.srv.store.path(sid)))

    def test_expiry(self):
        sid = self.upload(**{"X-TTL": "1"})
        time.sleep(1.2)
        self.assertEqual(self.call("GET", "/s/%s/meta" % sid, key=None)[0], 404)
        self.assertFalse(os.path.exists(self.srv.store.path(sid)))

    def test_ttl_is_capped(self):
        sid = self.upload(**{"X-TTL": "999999"})
        _, body, _ = self.call("GET", "/s/%s/meta" % sid, key=None)
        import json
        self.assertLessEqual(json.loads(body)["expires_at"], time.time() + 3601)

    def test_pin_and_lockout(self):
        sid = self.upload(**{"X-Pin": "1234"})
        meta = self.call("GET", "/s/%s/meta" % sid, key=None)[1]
        self.assertIn(b'"pin_required": true', meta)
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, key=None)[0], 403)
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, headers={"X-Pin": "1234"}, key=None)[0], 200)
        sid = self.upload(**{"X-Pin": "1234"})
        for _ in range(5):
            self.assertEqual(self.call("GET", "/s/%s/blob" % sid, headers={"X-Pin": "0000"}, key=None)[0], 403)
        # locked now, even for the right PIN
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, headers={"X-Pin": "1234"}, key=None)[0], 429)

    def test_size_and_quota(self):
        status, _, _ = self.call("POST", "/api/v1/shares", b"x" * (1024 * 1024 + 1))
        self.assertEqual(status, 413)
        self.upload(b"x" * 1024 * 1024)
        self.upload(b"x" * 1024 * 1024)
        self.assertEqual(self.call("POST", "/api/v1/shares", b"x" * 10)[0], 413)  # total 2 MiB reached

    def test_delete(self):
        sid = self.upload()
        self.assertEqual(self.call("DELETE", "/api/v1/shares/%s" % sid, key=None)[0], 401)
        self.assertEqual(self.call("DELETE", "/api/v1/shares/%s" % sid)[0], 200)
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, key=None)[0], 404)

    def test_sender_can_check_progress(self):
        sid = self.upload(**{"X-Max-Downloads": "2"})
        import json
        self.assertEqual(self.call("GET", "/api/v1/shares/%s" % sid, key=None)[0], 401)
        status, body, _ = self.call("GET", "/api/v1/shares/%s" % sid)
        self.assertEqual((status, json.loads(body)["downloads"]), (200, 0))
        self.call("GET", "/s/%s/blob" % sid, key=None)
        self.assertEqual(json.loads(self.call("GET", "/api/v1/shares/%s" % sid)[1])["downloads"], 1)
        self.call("DELETE", "/api/v1/shares/%s" % sid)
        self.assertEqual(self.call("GET", "/api/v1/shares/%s" % sid)[0], 404)

    def test_bad_ids_are_rejected(self):
        self.assertEqual(self.call("GET", "/s/../../etc/passwd/blob", key=None)[0], 404)
        self.assertEqual(self.call("GET", "/s/short/blob", key=None)[0], 404)


if __name__ == "__main__":
    unittest.main()
