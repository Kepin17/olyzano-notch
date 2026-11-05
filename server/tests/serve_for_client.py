"""Starts a relay with a cloud shelf (on a fake S3) for the app's own tests (tests/tst_cloud.cpp) and then takes commands on stdin:
   invite NAME      -> a new member's invitation link
   disable NAME WHY | enable NAME | remove NAME WHY | stored  -> prints a line of JSON for each
First line printed: {"url", "hostKey"}."""
import contextlib
import io
import json
import os
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import relay  # noqa: E402
from test_cloud import ACCESS, SECRET, FakeS3  # noqa: E402

HOST_KEY = "h" * 40
data = tempfile.TemporaryDirectory()
s3 = FakeS3()
threading.Thread(target=s3.serve_forever, daemon=True).start()
cfg = relay.Config({"RELAY_API_KEY": HOST_KEY, "DATA_DIR": data.name, "PORT": "0", "BIND": "127.0.0.1", "MAX_FILE_MB": "5",
                    "MAX_TOTAL_MB": "20", "S3_ENDPOINT": "http://127.0.0.1:%d" % s3.server_address[1], "S3_BUCKET": "bucket",
                    "S3_ACCESS_KEY": ACCESS, "S3_SECRET_KEY": SECRET})
srv = relay.make_server(cfg)
cfg.public_url = "http://127.0.0.1:%d" % srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
print(json.dumps({"url": cfg.public_url, "hostKey": HOST_KEY}), flush=True)


def cli(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        relay.cli(cfg, list(argv))
    return out.getvalue()


for line in sys.stdin:
    parts = line.rstrip("\n").split(" ", 2)
    cmd = parts[0]
    if cmd == "invite":
        cli("members", "add", parts[1], "--cloud-quota-mb", "50")
        link = cli("invite", parts[1]).strip().splitlines()[-1].strip()
        print(json.dumps({"link": link}), flush=True)
    elif cmd == "disable":
        cli("members", "disable", parts[1], "--reason", parts[2])
        print(json.dumps({"ok": True}), flush=True)
    elif cmd == "enable":
        cli("members", "enable", parts[1])
        print(json.dumps({"ok": True}), flush=True)
    elif cmd == "remove":
        cli("members", "remove", parts[1], "--reason", parts[2])
        print(json.dumps({"ok": True}), flush=True)
    elif cmd == "stored":
        blobs = list(s3.objects.values())
        print(json.dumps({"count": len(blobs), "plaintext": any(b"secret contents" in b for b in blobs),
                          "bad_signatures": s3.bad_signatures}), flush=True)
    elif cmd == "quit":
        break
