import http.client
import json
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

    def test_the_address_in_a_browser_shows_a_page_not_json(self):
        status, body, r = self.call("GET", "/", key=None)
        self.assertEqual(status, 200)
        self.assertIn("text/html", r.getheader("Content-Type"))
        self.assertIn(b"No files shared", body)
        status, body, r = self.call("GET", "/nothing/here", key=None, headers={"Accept": "text/html,application/xhtml+xml"})
        self.assertEqual(status, 404)
        self.assertIn("text/html", r.getheader("Content-Type"))
        status, body, r = self.call("GET", "/nothing/here", key=None)  # an API client still gets JSON
        self.assertEqual(status, 404)
        self.assertEqual(json.loads(body), {"error": "not found"})

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

    def test_single_use_survives_a_race(self):
        # Many requests for the final bytes at the same moment: exactly one gets the file.
        sid = self.upload(b"x" * 200_000, **{"X-Max-Downloads": "1"})
        results = []
        barrier = threading.Barrier(8)

        def go():
            barrier.wait()
            results.append(self.call("GET", "/s/%s/blob" % sid, key=None)[0])
        threads = [threading.Thread(target=go) for _ in range(8)]
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        self.assertEqual(sorted(results), [200] + [404] * 7)

    def test_a_download_that_breaks_is_given_back(self):
        sid = self.upload(b"x" * 100, **{"X-Max-Downloads": "1"})
        store = self.srv.store
        self.assertTrue(store.reserve(sid))
        store.release(sid)  # the transfer failed
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, key=None)[0], 200)
        self.assertEqual(self.call("GET", "/s/%s/blob" % sid, key=None)[0], 404)

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

    def test_meta_tells_how_many_digits_the_pin_has(self):
        def length(**headers):
            sid = self.upload(**headers)
            return json.loads(self.call("GET", "/s/%s/meta" % sid, key=None)[1])["pin_length"]
        self.assertEqual(length(**{"X-Pin": "482193"}), 6)
        self.assertEqual(length(**{"X-Pin": "1234"}), 4)
        self.assertEqual(length(), 0)                      # no PIN
        self.assertEqual(length(**{"X-Pin": "ab12"}), 0)   # not digits: the page shows a single field
        self.assertEqual(length(**{"X-Pin": "1" * 13}), 0)  # too long for boxes

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


class PeopleLimitTest(unittest.TestCase):
    """X-Max-People: how many different devices may start downloading (told apart by the address the proxy saw)."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        env = {"RELAY_API_KEY": KEY, "DATA_DIR": self.dir.name, "PORT": "0", "BIND": "127.0.0.1", "MAX_FILE_MB": "1",
               "MAX_TOTAL_MB": "4", "MAX_TTL_HOURS": "1", "TRUST_PROXY": "1", "PROXY_HOPS": "1"}
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
        return json.loads(body)["id"]

    def get(self, sid, ip, part="blob", **headers):
        return self.call("GET", "/s/%s/%s" % (sid, part), key=None, headers=dict({"X-Forwarded-For": ip}, **headers))[0]

    def test_the_ping_says_the_limit_is_understood(self):
        self.assertTrue(json.loads(self.call("GET", "/api/v1/ping")[1])["people_limit"])

    def test_only_that_many_people_may_start(self):
        sid = self.upload(**{"X-Max-People": "2"})
        self.assertEqual(self.get(sid, "10.0.0.1"), 200)
        self.assertEqual(self.get(sid, "10.0.0.2"), 200)
        self.assertEqual(self.get(sid, "10.0.0.3"), 423)  # the third
        self.assertEqual(self.get(sid, "10.0.0.1"), 200)  # one of the two: again is fine
        # the page and its meta are not a place; a HEAD is not either
        self.assertEqual(self.get(sid, "10.0.0.3", "meta"), 200)
        self.assertEqual(self.call("HEAD", "/s/%s/blob" % sid, key=None, headers={"X-Forwarded-For": "10.0.0.3"})[0], 200)
        meta = json.loads(self.call("GET", "/s/%s/meta" % sid, key=None)[1])
        self.assertEqual((meta["max_people"], meta["max_downloads"]), (2, 0))
        progress = json.loads(self.call("GET", "/api/v1/shares/%s" % sid)[1])
        self.assertEqual((progress["people"], progress["max_people"], progress["downloads"]), (2, 2, 3))

    def test_the_addresses_are_not_kept(self):
        sid = self.upload(**{"X-Max-People": "3"})
        self.assertEqual(self.get(sid, "198.51.100.77"), 200)
        with open(self.srv.store.path(sid, "meta.json")) as f:
            raw = f.read()
        self.assertNotIn("198.51.100.77", raw)
        self.assertEqual(len(json.loads(raw)["people"]), 1)

    def test_no_limit_by_default_and_old_headers_still_work(self):
        sid = self.upload()
        for n in range(1, 8):
            self.assertEqual(self.get(sid, "10.0.0.%d" % n), 200)
        one = self.upload(**{"X-Max-Downloads": "1"})
        self.assertEqual(self.get(one, "10.0.0.1"), 200)
        self.assertEqual(self.get(one, "10.0.0.2"), 404)  # used up

    def test_downloads_and_people_together(self):
        sid = self.upload(**{"X-Max-People": "2", "X-Max-Downloads": "3"})
        for ip in ("10.0.0.1", "10.0.0.2", "10.0.0.1"):
            self.assertEqual(self.get(sid, ip), 200)
        self.assertEqual(self.get(sid, "10.0.0.2"), 404)  # three downloads: it ends before anything else
        self.assertFalse(os.path.exists(self.srv.store.path(sid)))

    def test_the_last_place_goes_to_one_device_in_a_race(self):
        sid = self.upload(b"x" * 100_000, **{"X-Max-People": "1"})
        results = []
        barrier = threading.Barrier(8)

        def go(n):
            barrier.wait()
            results.append(self.get(sid, "10.0.1.%d" % n))
        threads = [threading.Thread(target=go, args=(n,)) for n in range(8)]
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        self.assertEqual(sorted(results), [200] + [423] * 7)


class DownloadEventsTest(PeopleLimitTest):
    """What the sender's app is told: who took the share (a device's words, never an address), and who was turned away."""

    UA_IPHONE = "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1"
    UA_ANDROID = "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36"

    def progress(self, sid, since=None):
        path = "/api/v1/shares/%s" % sid + ("" if since is None else "?since=%d" % since)
        return json.loads(self.call("GET", path)[1])

    def fetch(self, sid, ip, ua):
        return self.call("GET", "/s/%s/blob" % sid, key=None, headers={"X-Forwarded-For": ip, "User-Agent": ua})[0]

    def test_the_words_for_a_device(self):
        self.assertEqual(relay.device_label(self.UA_IPHONE), "iPhone \u00b7 Safari")
        self.assertEqual(relay.device_label(self.UA_ANDROID), "Android \u00b7 Chrome")
        self.assertEqual(relay.device_label("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/120 Safari/537.36 Edg/120.0"), "Windows \u00b7 Edge")
        self.assertEqual(relay.device_label("Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:121.0) Gecko/20100101 Firefox/121.0"), "Mac \u00b7 Firefox")
        self.assertEqual(relay.device_label("curl/8.5.0"), "curl")
        self.assertEqual(relay.device_label(""), "A device")
        self.assertEqual(relay.device_label("x" * 5000), "A device")

    def test_a_download_is_an_event_and_since_gives_only_the_new_ones(self):
        sid = self.upload(**{"X-Max-People": "2"})
        self.assertEqual(self.progress(sid, -1)["events"], [])  # the first look only tells the place
        place = self.progress(sid, -1)["events_next"]
        self.assertEqual(place, 0)
        self.assertEqual(self.fetch(sid, "10.0.0.1", self.UA_IPHONE), 200)
        p = self.progress(sid, place)
        self.assertEqual(p["events"], [{"kind": "download", "device": "iPhone \u00b7 Safari"}])
        self.assertEqual(self.progress(sid, p["events_next"])["events"], [])
        self.assertEqual(self.fetch(sid, "10.0.0.2", self.UA_ANDROID), 200)
        self.assertEqual(self.fetch(sid, "10.0.0.3", self.UA_ANDROID), 423)  # the third is turned away
        p2 = self.progress(sid, p["events_next"])
        self.assertEqual([e["kind"] for e in p2["events"]], ["download", "turned_away"])
        self.assertEqual(p2["events"][1]["device"], "Android \u00b7 Chrome")

    def test_no_address_is_kept_with_the_events(self):
        sid = self.upload()
        self.fetch(sid, "198.51.100.9", self.UA_IPHONE)
        with open(self.srv.store.path(sid, "meta.json")) as f:
            raw = f.read()
        self.assertNotIn("198.51.100.9", raw)
        self.assertNotIn("AppleWebKit", raw)  # the words only, not the User-Agent

    def test_only_the_last_twenty_events_are_kept(self):
        sid = self.upload()
        for n in range(30):
            self.fetch(sid, "10.0.%d.1" % n, self.UA_ANDROID)
        p = self.progress(sid, 0)
        self.assertEqual(len(p["events"]), 20)
        self.assertEqual(p["events_next"], 30)


class FormsRelayTest(unittest.TestCase):
    """Forms on the relay: the sealed definition, answers as blobs the server cannot read, the limits, and only the owner reading them."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        env = {"RELAY_API_KEY": KEY, "DATA_DIR": self.dir.name, "PORT": "0", "BIND": "127.0.0.1", "MAX_FILE_MB": "2", "MAX_TOTAL_MB": "6",
               "MAX_TTL_HOURS": "1", "TRUST_PROXY": "1", "PROXY_HOPS": "1"}
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

    DEF = b"sealed-definition-bytes" * 10

    def make(self, key=KEY, **headers):
        status, body, _ = self.call("POST", "/api/v1/forms", self.DEF, headers, key)
        self.assertEqual(status, 201, body)
        return json.loads(body)["id"]

    def answer(self, fid, ip, blob=b"sealed-answer", ua="Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) Version/17.0 Mobile Safari/604.1", **headers):
        h = dict({"X-Forwarded-For": ip, "User-Agent": ua}, **headers)
        return self.call("POST", "/f/%s/submit" % fid, blob, h, key=None)[0]

    def member_key(self, name, **limits):
        self.srv.members.add(name, **limits)
        got = self.srv.members.redeem(self.srv.members.invite(name), "9.9.9.9")
        return got[1]

    def test_the_server_says_it_does_forms(self):
        self.assertTrue(json.loads(self.call("GET", "/api/v1/ping")[1])["forms"])

    def test_an_answer_can_be_changed_from_a_personal_link_when_the_form_allows_it(self):
        H = "a" * 64
        fid = self.make(**{"X-Max-Responses": "2", "X-Max-People": "1", "X-Allow-Edit": "1"})
        self.assertEqual(self.answer(fid, "9.0.0.1"), 201)
        # the sealed copy a person changes the answer from: kept, given back, only for a well-formed hash
        put = lambda h, body, ip="9.0.0.1": self.call("POST", "/f/%s/edit/%s" % (fid, h), body, {"X-Forwarded-For": ip}, key=None)
        get = lambda h, ip="9.0.0.9": self.call("GET", "/f/%s/edit/%s" % (fid, h), None, {"X-Forwarded-For": ip}, key=None)
        self.assertEqual(get(H)[0], 404)
        self.assertEqual(put(H, b"sealed-copy-1")[0], 200)
        status, data, _ = get(H)
        self.assertEqual((status, data), (200, b"sealed-copy-1"))
        self.assertEqual(put("zz", b"x")[0], 404)
        self.assertEqual(put("b" * 64, b"x" * 40000)[0], 413)
        # the owner has it already (and acknowledges): the copy stays for the person
        status, body, _ = self.call("GET", "/api/v1/forms/%s/responses" % fid)
        self.assertEqual(len(json.loads(body)["responses"]), 1)
        self.call("DELETE", "/api/v1/forms/%s/responses/1" % fid)
        self.assertEqual(get(H)[1], b"sealed-copy-1")
        # a change: from another device (the one person allowed has started), without a place, and only for a copy that exists
        self.assertEqual(self.answer(fid, "9.0.0.7", b"sealed-answer-2", **{"X-Edit-Replace": H}), 201)
        self.assertEqual(self.answer(fid, "9.0.0.7", b"sealed-answer-3", **{"X-Edit-Replace": "c" * 64}), 404)
        self.assertEqual(self.answer(fid, "9.0.0.7", b"sealed-answer-3", **{"X-Edit-Replace": "bad"}), 404)
        self.assertEqual(self.answer(fid, "9.0.0.8", b"third-person"), 423)  # a new person is still turned away
        status, body, _ = self.call("GET", "/api/v1/forms/%s/responses" % fid)
        info = json.loads(body)
        self.assertEqual(info["received"], 1)  # a change is not another answer
        self.assertEqual([r["replaces"] for r in info["responses"]], [True])
        self.assertEqual(put(H, b"sealed-copy-2")[0], 200)
        self.assertEqual(get(H)[1], b"sealed-copy-2")
        # a form that takes no changes keeps no copies and takes no replacements
        plain = self.make()
        self.assertEqual(self.call("POST", "/f/%s/edit/%s" % (plain, H), b"x", {"X-Forwarded-For": "9.0.0.1"}, key=None)[0], 404)
        self.assertEqual(self.answer(plain, "9.0.0.1", b"y", **{"X-Edit-Replace": H}), 404)
        # ping says so
        self.assertTrue(json.loads(self.call("GET", "/api/v1/ping")[1]).get("forms_edit"))
        # the copies are held with the form: gone with it
        self.call("DELETE", "/api/v1/forms/" + fid)
        self.assertEqual(get(H)[0], 404)

    def test_a_form_can_take_one_answer_from_each_device(self):
        once = self.make(**{"X-One-Answer": "1"})
        self.assertEqual(self.answer(once, "9.1.0.1"), 201)
        self.assertEqual(self.answer(once, "9.1.0.1"), 409)  # the same device again
        self.assertEqual(self.answer(once, "9.1.0.2"), 201)  # another one
        # two at once from one new device: only one gets in
        results = []
        threads = [threading.Thread(target=lambda: results.append(self.answer(once, "9.1.0.3"))) for _ in range(4)]
        [t.start() for t in threads]; [t.join() for t in threads]
        self.assertEqual(sorted(results).count(201), 1, results)
        info = json.loads(self.call("GET", "/api/v1/forms/%s/responses" % once)[1])
        self.assertEqual(info["received"], 3)
        # only a hash of the address is kept
        meta = open(os.path.join(self.dir.name, "forms", once, "meta.json")).read()
        self.assertNotIn("9.1.0.1", meta)
        # a form that does not say so takes as many as they like, and the ping tells the app it can
        many = self.make()
        self.assertEqual([self.answer(many, "9.1.0.1") for _ in range(3)], [201, 201, 201])
        self.assertTrue(json.loads(self.call("GET", "/api/v1/ping")[1]).get("forms_once"))

    def test_a_form_that_opens_later_takes_no_answers_before_then(self):
        later = int(time.time()) + 3600
        fid = self.make(**{"X-Opens-At": str(later), "X-Max-People": "1"})
        meta = json.loads(self.call("GET", "/f/%s/meta" % fid, key=None)[1])
        self.assertEqual(meta["opens_at"], later)
        self.assertLess(abs(meta["now"] - time.time()), 3)
        status, body, _ = self.call("POST", "/f/%s/submit" % fid, b"sealed", {"X-Forwarded-For": "10.0.0.1"}, key=None)
        self.assertEqual(status, 425)
        self.assertEqual(json.loads(body)["opens_at"], later)
        self.assertEqual(json.loads(self.call("GET", "/api/v1/forms/%s/responses" % fid)[1])["people"], 0)  # nobody took the one place
        self.assertEqual(self.call("GET", "/f/%s/def" % fid, key=None)[0], 200)  # the sealed form itself can be read (the page needs it to say what it is)

    def test_a_form_that_has_opened_takes_answers_and_old_clients_open_at_once(self):
        fid = self.make(**{"X-Opens-At": str(int(time.time()) - 5)})
        self.assertEqual(self.answer(fid, "10.0.0.1"), 201)
        plain = self.make()  # no header: as before
        self.assertEqual(json.loads(self.call("GET", "/f/%s/meta" % plain, key=None)[1])["opens_at"], 0)
        self.assertEqual(self.answer(plain, "10.0.0.1"), 201)
        self.assertTrue(json.loads(self.call("GET", "/api/v1/ping")[1])["forms_schedule"])

    def test_a_form_is_made_read_and_answered(self):
        fid = self.make(**{"X-Max-Responses": "5", "X-TTL": "600"})
        status, page, r = self.call("GET", "/f/%s" % fid, key=None)
        self.assertEqual(status, 200)
        self.assertIn(b"form-data", page)
        self.assertIn("script-src", r.getheader("Content-Security-Policy"))
        meta = json.loads(self.call("GET", "/f/%s/meta" % fid, key=None)[1])
        self.assertEqual((meta["pin_required"], meta["full"]), (False, False))
        self.assertEqual(self.call("GET", "/f/%s/def" % fid, key=None)[1], self.DEF)  # exactly what the owner's app sealed
        self.assertEqual(self.answer(fid, "10.0.0.1", b"first sealed answer"), 201)
        self.assertEqual(self.answer(fid, "10.0.0.2", b"second"), 201)
        listing = json.loads(self.call("GET", "/api/v1/forms/%s/responses" % fid)[1])
        self.assertEqual([r["n"] for r in listing["responses"]], [1, 2])
        self.assertEqual(listing["responses"][0]["device"], "iPhone \u00b7 Safari")  # the words, not the User-Agent
        self.assertEqual((listing["received"], listing["people"], listing["max_responses"]), (2, 2, 5))
        self.assertEqual(self.call("GET", "/api/v1/forms/%s/responses/1" % fid)[1], b"first sealed answer")
        self.assertEqual([r["n"] for r in json.loads(self.call("GET", "/api/v1/forms/%s/responses?after=1" % fid)[1])["responses"]], [2])

    def test_the_app_acknowledges_an_answer_and_the_room_comes_back(self):
        fid = self.make()
        self.answer(fid, "10.0.0.1", b"x" * 100_000)
        used = self.srv.store.used()
        self.assertGreater(used, 100_000)
        self.assertEqual(self.call("DELETE", "/api/v1/forms/%s/responses/1" % fid)[0], 200)
        self.assertLess(self.srv.store.used(), used - 90_000)
        self.assertEqual(self.call("GET", "/api/v1/forms/%s/responses/1" % fid)[0], 404)
        # the answer still counts as received: the form does not take more than it was told to
        self.assertEqual(json.loads(self.call("GET", "/api/v1/forms/%s/responses" % fid)[1])["received"], 1)

    def test_the_form_closes_after_its_last_answer(self):
        fid = self.make(**{"X-Max-Responses": "2"})
        self.assertEqual(self.answer(fid, "10.0.0.1"), 201)
        self.assertEqual(self.answer(fid, "10.0.0.2"), 201)
        self.assertEqual(self.answer(fid, "10.0.0.3"), 410)
        self.assertTrue(json.loads(self.call("GET", "/f/%s/meta" % fid, key=None)[1])["full"])

    def test_only_that_many_people_may_answer(self):
        fid = self.make(**{"X-Max-People": "2", "X-Max-Responses": "20"})
        self.assertEqual(self.answer(fid, "10.0.0.1"), 201)
        self.assertEqual(self.answer(fid, "10.0.0.2"), 201)
        self.assertEqual(self.answer(fid, "10.0.0.3"), 423)
        self.assertEqual(self.answer(fid, "10.0.0.1"), 201)  # one of the two: again is fine
        with open(self.srv.forms.path(fid, "meta.json")) as f:
            raw = f.read()
        self.assertNotIn("10.0.0.1", raw)  # a hash, never the address

    def test_a_pin_gates_the_definition_and_the_answers(self):
        fid = self.make(**{"X-Pin": "4821"})
        meta = json.loads(self.call("GET", "/f/%s/meta" % fid, key=None)[1])
        self.assertEqual((meta["pin_required"], meta["pin_length"]), (True, 4))
        self.assertEqual(self.call("GET", "/f/%s/def" % fid, key=None, headers={"X-Forwarded-For": "10.0.0.1"})[0], 403)
        self.assertEqual(self.call("GET", "/f/%s/def" % fid, key=None, headers={"X-Forwarded-For": "10.0.0.1", "X-Pin": "4821"})[0], 200)
        self.assertEqual(self.answer(fid, "10.0.0.1"), 403)
        self.assertEqual(self.answer(fid, "10.0.0.1", X_Pin="4821") if False else self.answer(fid, "10.0.0.1", **{"X-Pin": "4821"}), 201)
        for _ in range(5):
            self.call("GET", "/f/%s/def" % fid, key=None, headers={"X-Forwarded-For": "10.0.0.9", "X-Pin": "0000"})
        self.assertEqual(self.call("GET", "/f/%s/def" % fid, key=None, headers={"X-Forwarded-For": "10.0.0.9", "X-Pin": "4821"})[0], 429)

    def test_an_answer_over_the_size_is_refused_and_a_big_one_never_lands(self):
        fid = self.make(**{"X-Max-Response-Bytes": str(1 * relay.MB)})
        self.assertEqual(self.answer(fid, "10.0.0.1", b"x" * (1 * relay.MB + 1)), 413)
        self.assertEqual(self.answer(fid, "10.0.0.1", b"x" * (1 * relay.MB)), 201)
        self.assertEqual(os.listdir(self.srv.store.tmp), [])  # nothing is left half-written

    def test_only_the_owner_reads_the_answers(self):
        fid = self.make()
        self.answer(fid, "10.0.0.1", b"secret")
        alice = self.member_key("alice")
        self.assertEqual(self.call("GET", "/api/v1/forms/%s/responses" % fid, key=alice)[0], 404)
        self.assertEqual(self.call("GET", "/api/v1/forms/%s/responses/1" % fid, key=alice)[0], 404)
        self.assertEqual(self.call("DELETE", "/api/v1/forms/%s" % fid, key=alice)[0], 200)  # told "ok", but it is not hers
        self.assertEqual(self.call("GET", "/api/v1/forms/%s/responses/1" % fid)[1], b"secret")
        self.assertEqual(self.call("GET", "/api/v1/forms/%s/responses" % fid, key=None)[0], 401)
        self.assertEqual(self.call("GET", "/api/v1/forms/%s/responses" % fid, key="wrong")[0], 401)
        self.assertEqual(self.call("POST", "/api/v1/forms", self.DEF, key=None)[0], 401)

    def test_the_owner_deletes_a_form_and_it_ends_on_time(self):
        fid = self.make()
        self.assertEqual(self.call("DELETE", "/api/v1/forms/%s" % fid)[0], 200)
        self.assertEqual(self.call("GET", "/f/%s/meta" % fid, key=None)[0], 404)
        self.assertFalse(os.path.exists(self.srv.forms.path(fid)))
        short = self.make(**{"X-TTL": "1"})
        time.sleep(1.2)
        self.assertEqual(self.call("GET", "/f/%s/def" % short, key=None)[0], 404)
        self.assertEqual(self.answer(short, "10.0.0.1"), 404)

    def test_bad_requests(self):
        self.assertEqual(self.call("POST", "/api/v1/forms", b"", {})[0], 400)
        self.assertEqual(self.call("POST", "/api/v1/forms", self.DEF, {"X-Max-Responses": "0"})[0], 400)
        self.assertEqual(self.call("POST", "/api/v1/forms", self.DEF, {"X-Max-Responses": "5000"})[0], 400)
        self.assertEqual(self.call("POST", "/api/v1/forms", b"x" * (70 * 1024), {})[0], 413)
        self.assertEqual(self.call("GET", "/f/not-a-real-form-id", key=None)[0], 404)
        self.assertEqual(self.call("GET", "/f/../etc/passwd", key=None)[0], 404)
        self.assertEqual(self.answer("not-a-real-form-id", "10.0.0.1"), 404)

    def test_forms_take_room_from_the_members_space_and_are_counted(self):
        alice = self.member_key("alice", quota_mb=1)
        fid = self.make(key=alice, **{"X-Max-Response-Bytes": str(1 * relay.MB)})
        self.assertEqual(self.answer(fid, "10.0.0.1", b"x" * (700 * 1024)), 201)
        self.assertEqual(self.answer(fid, "10.0.0.2", b"x" * (700 * 1024)), 413)  # her space is full: the form cannot take more until she fetches
        self.call("DELETE", "/api/v1/forms/%s/responses/1" % fid, key=alice)
        self.assertEqual(self.answer(fid, "10.0.0.2", b"x" * (700 * 1024)), 201)

    def test_a_member_has_a_limit_of_forms_open(self):
        for _ in range(relay.forms_mod.MAX_FORMS_PER_OWNER):
            self.make()
        status, body, _ = self.call("POST", "/api/v1/forms", self.DEF, {})
        self.assertEqual(status, 429)


if __name__ == "__main__":
    unittest.main()
