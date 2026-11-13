"""Forms on the relay: a person who has the link opens a page, fills it in, and their answer (and files) goes up as one encrypted blob that only the
form's owner's app can open. The server holds the sealed form definition and the sealed answers, counts them and keeps to the limits; it can
read none of it. Layout: <data>/forms/<id>/{def.blob, meta.json, responses/<n>.blob, responses/<n>.json}."""
import hashlib
import json
import os
import secrets
import shutil
import threading
import time

FORM_ID_LEN = 16
MAX_FORMS_PER_OWNER = 20
MAX_DEF_BYTES = 64 * 1024
MAX_EDIT_BYTES = 32 * 1024
MAX_RESPONSES = 1000
RATE_WINDOW, RATE_MAX = 600, 30  # answers per address per ten minutes


class RelayForms:
    def __init__(self, cfg, id_re, owner):
        self.root = os.path.join(cfg.data_dir, "forms")
        self.id_re = id_re
        self.owner_default = owner
        self.lock = threading.Lock()
        self.sent = {}  # (id, ip) -> [times]
        os.makedirs(self.root, exist_ok=True)

    # ---- paths and meta ----
    def path(self, fid, *parts):
        return os.path.join(self.root, fid, *parts)

    def meta(self, fid):
        if not self.id_re.match(fid):
            return None
        try:
            with open(self.path(fid, "meta.json")) as f:
                m = json.load(f)
        except (OSError, ValueError):
            return None
        if m["expires_at"] <= time.time():
            self.delete(fid)
            return None
        return m

    def save_meta(self, fid, m):
        tmp = self.path(fid, "meta.json.tmp")
        with open(tmp, "w") as f:
            json.dump(m, f)
        os.replace(tmp, self.path(fid, "meta.json"))

    def delete(self, fid):
        if self.id_re.match(fid):
            shutil.rmtree(self.path(fid), ignore_errors=True)

    def ids(self):
        try:
            return [d for d in os.listdir(self.root) if self.id_re.match(d)]
        except OSError:
            return []

    def used(self, owner=None):
        """Bytes held by forms (the sealed definitions and the answers waiting to be fetched): all of them, or one member's."""
        total = 0
        for fid in self.ids():
            m = self.meta(fid)
            if m and (owner is None or m.get("owner", self.owner_default) == owner):
                total += m.get("bytes", 0)
        return total

    def count(self, owner):
        return sum(1 for fid in self.ids() if (m := self.meta(fid)) and m.get("owner", self.owner_default) == owner)

    def sweep(self):
        for fid in self.ids():
            self.meta(fid)

    # ---- the owner ----
    def create(self, owner, definition, ttl, max_responses, max_people, max_bytes, quota, pin_salt, pin_hash, pin_len, opens_at=0, allow_edit=False, one_answer=False):
        fid = secrets.token_urlsafe(FORM_ID_LEN)
        while fid.startswith("-"):
            fid = secrets.token_urlsafe(FORM_ID_LEN)
        os.makedirs(self.path(fid, "responses"))
        with open(self.path(fid, "def.blob"), "wb") as f:
            f.write(definition)
        expires = int(time.time()) + ttl
        self.save_meta(fid, {
            "owner": owner, "expires_at": expires, "opens_at": opens_at, "created": int(time.time()), "max_responses": max_responses, "max_people": max_people,
            "max_bytes": max_bytes, "quota": quota, "received": 0, "next_n": 1, "bytes": len(definition), "people": [], "people_salt": secrets.token_hex(8),
            "pin_len": pin_len, "pin_salt": pin_salt, "pin_hash": pin_hash, "allow_edit": bool(allow_edit),
            "one_answer": bool(one_answer), "answered": [],
        })
        return fid, expires

    def responses(self, fid, after=0):
        out = []
        try:
            names = os.listdir(self.path(fid, "responses"))
        except OSError:
            return out
        for name in names:
            if name.endswith(".json") and name[:-5].isdigit() and int(name[:-5]) > after:
                try:
                    with open(self.path(fid, "responses", name)) as f:
                        out.append(json.load(f))
                except (OSError, ValueError):
                    pass
        return sorted(out, key=lambda r: r["n"])

    def response_path(self, fid, n):
        return self.path(fid, "responses", "%d.blob" % n)

    def drop_response(self, fid, n):
        """The owner's app has it: the blob goes, and the room it took is given back."""
        with self.lock:
            m = self.meta(fid)
            if not m:
                return False
            size = 0
            for ext in ("blob", "json"):
                p = self.path(fid, "responses", "%d.%s" % (n, ext))
                if ext == "blob":
                    try:
                        size = os.path.getsize(p)
                    except OSError:
                        size = 0
                try:
                    os.remove(p)
                except OSError:
                    pass
            m["bytes"] = max(0, m.get("bytes", 0) - size)
            self.save_meta(fid, m)
            return True

    # ---- a visitor ----
    def admit(self, fid, ip):
        """-> "ok", "full" (the people allowed have all started), "closed" (all the answers are in) or "gone". Atomic, so two new devices
        racing for the last place cannot both win. Only a hash of the address is kept."""
        with self.lock:
            m = self.meta(fid)
            if not m:
                return "gone"
            if m["received"] >= m["max_responses"]:
                return "closed"
            people = m.setdefault("people", [])
            who = hashlib.sha256((m.get("people_salt", "") + ip).encode()).hexdigest()[:16]
            if who in people:
                return "ok"
            if 0 < m.get("max_people", 0) <= len(people):
                return "full"
            people.append(who)
            self.save_meta(fid, m)
            return "ok"

    def rate_ok(self, fid, ip):
        now = time.time()
        with self.lock:
            times = [t for t in self.sent.get((fid, ip), []) if t > now - RATE_WINDOW]
            if len(times) >= RATE_MAX:
                self.sent[(fid, ip)] = times
                return False
            times.append(now)
            self.sent[(fid, ip)] = times
        return True

    # ---- the copy a person can change their answer from: sealed in their browser, small, one for each personal link ----
    def edit_path(self, fid, h):
        return self.path(fid, "edits", h + ".blob")

    def has_edit(self, fid, h):
        return os.path.isfile(self.edit_path(fid, h))

    def load_edit(self, fid, h):
        try:
            with open(self.edit_path(fid, h), "rb") as f:
                return f.read()
        except OSError:
            return None

    def save_edit(self, fid, h, data):
        """-> "ok", "no" (this form takes no changes) or "full" (as many copies as the form takes answers)."""
        with self.lock:
            m = self.meta(fid)
            if not m or not m.get("allow_edit"):
                return "no"
            os.makedirs(self.path(fid, "edits"), exist_ok=True)
            old = 0
            try:
                old = os.path.getsize(self.edit_path(fid, h))
            except OSError:
                if len(os.listdir(self.path(fid, "edits"))) >= m["max_responses"]:
                    return "full"
            with open(self.edit_path(fid, h), "wb") as f:
                f.write(data)
            m["bytes"] = max(0, m.get("bytes", 0) - old + len(data))
            self.save_meta(fid, m)
            return "ok"

    def has_answered(self, fid, ip):
        """True when this device has already sent an answer to a form that takes one from each (only a hash of the address is kept)."""
        m = self.meta(fid)
        if not m or not m.get("one_answer"):
            return False
        return hashlib.sha256((m.get("people_salt", "") + ip).encode()).hexdigest()[:16] in m.get("answered", [])

    def add_response(self, fid, staged, size, device, replace=False, ip=None):
        """Keeps an answer that was staged in a temporary file: -> its number, or None when the form closed meanwhile."""
        with self.lock:
            m = self.meta(fid)
            if not m or (not replace and m["received"] >= m["max_responses"]):
                return None
            who = hashlib.sha256((m.get("people_salt", "") + ip).encode()).hexdigest()[:16] if ip and m.get("one_answer") and not replace else None
            if who and who in m.setdefault("answered", []):  # decided here, under the lock: two answers sent at once cannot both get in
                return "already"
            if who:
                m["answered"].append(who)
            n = m["next_n"]
            os.replace(staged, self.response_path(fid, n))
            with open(self.path(fid, "responses", "%d.json" % n), "w") as f:
                json.dump({"n": n, "size": size, "time": int(time.time()), "device": device, "replaces": bool(replace)}, f)
            m["next_n"], m["received"], m["bytes"] = n + 1, m["received"] + (0 if replace else 1), m.get("bytes", 0) + size  # a changed answer takes no place
            self.save_meta(fid, m)
            return n
