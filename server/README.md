# Notch share relay

A tiny server for Notch's **Share via QR → Your server** mode. Notch encrypts the file on your
computer (AES-256-GCM) and uploads the ciphertext here; the key stays in the link's `#fragment`,
so this server can never read the file or even its name. Shares disappear on their own.

## Install (Docker Compose)

1. Point a DNS record for a subdomain (e.g. `share.example.com`) to your VPS and open ports 80 and 443.
2. Copy this `server/` folder to the VPS, then:

   ```bash
   cp .env.example .env
   openssl rand -hex 32        # paste the result as RELAY_API_KEY in .env, and set DOMAIN
   docker compose up -d --build
   ```

   Caddy gets the HTTPS certificate by itself the first time the domain is visited.
3. In Notch: **Settings → Sharing**, enter `https://share.example.com` and the API key, then
   **Test connection**.

Update: copy the new files, then `docker compose up -d --build`. Logs: `docker compose logs -f relay`
(they list methods and status codes only, never links).

## For a team: members, quotas, invitations

By default the relay has one key (`RELAY_API_KEY`), the host's. To let others use it, give each person their
own key with their own limits. The easiest way is in Notch itself: **Settings → Sharing**, on the server where you used
the host key, type a username and press **Create key**; copy the key and send it to them. They paste it under
**Join a server** (the key is a single-use invitation link, so it can be sent through chat). The same is possible from the command line (inside Docker prefix with
`docker compose exec relay`):

```bash
python3 relay.py members add alice --quota-mb 2000 --max-file-mb 200 --max-ttl-hours 48 --max-active 10
```

That prints a single-use **invitation link** (valid 7 days). Alice pastes it into Notch (**Settings → Sharing →
Join a server**); Notch trades it for a key of her own, so no key ever travels through chat. Set `PUBLIC_URL`
(docker compose does it from `DOMAIN`; behind nginx set it yourself) so the link carries your address.

| Command | What it does |
|---|---|
| `members list` | each member, their limits, and what they use now |
| `members set NAME --quota-mb N ...` | change limits (no restart needed) |
| `members revoke NAME` | the key stops working and all their shares are deleted |
| `invite NAME` | a new invitation; using it also gives them a new key (a lost key) |
| `shares list` / `shares kill ID` | live shares (owner, size, expiry, downloads) / delete one now |

Limits per member: disk used by their live shares, largest upload, longest expiry (a longer request is shortened,
not refused), and shares at the same time. They can never exceed the server's own `MAX_*`. A member sees only
their own shares; the host's key (`RELAY_API_KEY`) sees all. Notch shows each person their usage and greys out
what the server would refuse.

Because everything is end-to-end encrypted, the host can see **who** shared **how much** and **when**, but not
names or contents.

## Cloud shelf (optional)

The relay can also keep a **shared shelf** for your members, on an S3-compatible store of your own (MinIO, Backblaze B2,
Wasabi, Cloudflare R2, AWS S3...). Members never see the store's key: they use their own relay key, the relay signs and
streams, and so **you** decide who may use it and how much.

1. Make a bucket and a key on your store. With the bundled MinIO: set `S3_ENDPOINT=http://minio:9000`, `S3_ACCESS_KEY`,
   `S3_SECRET_KEY` and `S3_BUCKET` in `.env`, run `docker compose --profile minio up -d --build`, then create the bucket
   once with the `minio/mc` image on the same network:
   `NET=$(docker inspect -f '{{range $k,$v := .NetworkSettings.Networks}}{{$k}}{{end}}' $(docker compose ps -q minio))` then
   `docker run --rm --network $NET --entrypoint sh minio/mc -c "mc alias set l http://minio:9000 ACCESS SECRET && mc mb -p l/notch"`
   (the MinIO image itself has no `mc`).
   to that one bucket in `.env`.
2. Without `S3_*` the cloud shelf is simply off (the rest of the relay is unchanged).
3. In Notch (**Settings → Files → Cloud shelf**) pick this server. Files and their names are encrypted on the member's
   computer with a shelf key that only the people you invite have: the store and this relay see sizes and who owns what,
   never names or contents. The host's invitation link carries that key in its `#fragment`.

| Command | What it does |
|---|---|
| `members add NAME --cloud-quota-mb N` / `members set NAME --cloud-quota-mb N` | the space a member may use on the cloud shelf |
| `members disable NAME --reason "..."` | pause a member: every call is refused and the app shows them your reason |
| `members enable NAME` | let them in again |
| `members remove NAME --reason "..." [--delete-files]` | remove for good; their cloud files are deleted with `--delete-files`, otherwise they become yours |

The same actions are in Notch (**Settings → Sharing → members**) and over HTTP: `POST /api/v1/members/NAME/limits`,
`/disable` (`{"reason": "..."}`), `/enable`, and `DELETE /api/v1/members/NAME` (`{"reason": "...", "delete_files": false}`).
A member who is paused gets `403 {"error":"disabled","reason":...}`; one who was removed gets `401 {"error":"removed",...}`;
`GET /api/v1/ping` tells a paused member's app why, in plain words. Limits: `CLOUD_MAX_TOTAL_MB` for the whole shelf, then each
member's own `cloud_quota_mb`, and `MAX_FILE_MB` for one file (one streamed upload, up to 5 GiB).

## What the recipient sees

The page is flat and plain, in the system's light or dark. A PIN is typed into one box for each digit (the relay tells the page only how many digits the PIN has, never which; a password, or a share made before this was added, gets a single field).

One page for single files and for lists: image thumbnails, per-file download, tick several or take all as a
zip (made in their browser; for very large selections the page suggests downloading one by one). The page
is `page.html`, the same file Notch serves for "This WiFi" shares, so both look alike. A file picked from a
multi-file share is fetched with a byte `Range`, so only that file travels, never the whole bundle.

## Limits and safety

- Upload needs the API key; download needs the long random link (and the PIN, if set).
- Forms: `POST /api/v1/forms` (a sealed definition and `X-Max-Responses`, `X-Max-People`, `X-Max-Response-Bytes`), the page at `/f/<id>`, answers as sealed blobs
  at `POST /f/<id>/submit`, fetched and acknowledged by the owner with `GET`/`DELETE /api/v1/forms/<id>/responses[/n]`. The server only counts and keeps them (up to
  20 forms per member; the answers waiting count in the member's space). `GET /api/v1/ping` says `"forms": true` (and `"forms_once": true` when it can take one answer from each device (`X-One-Answer`, `409 already`), `"forms_edit": true` when it keeps sealed copies for personal links (`X-Allow-Edit`, `POST`/`GET /f/<id>/edit/<hash>`, `X-Edit-Replace` on submit), `"forms_schedule": true` when it understands `X-Opens-At`: a form that opens later answers `425` until then). Update the server (copy `relay.py`, `forms.py`,
  `form.html` and the rest, then `docker compose up -d --build`) to use *Documents → Forms → My server*.
- Each share has an expiry (up to `MAX_TTL_HOURS`) and can be limited to N downloads (`X-Max-Downloads`) and to N different
  people (`X-Max-People`): the devices are told apart by the address the proxy saw (`TRUST_PROXY`/`PROXY_HOPS`, as for the
  PIN lock), only a hash of each is kept, and a device that comes after the places are used up gets `423`. Update the relay
  to use the limit on people: `GET /api/v1/ping` says `"people_limit": true` when it is understood, and Notch refuses to
  start such a share on a server that does not say so.
- Wrong PIN: 5 tries, then that IP is locked out of that share for 10 minutes. (A password never reaches the
  server: it locks the key inside the link, and the page unlocks it in the browser.)
- `MAX_FILE_MB` per file, `MAX_TOTAL_MB` in total; uploads beyond that get HTTP 413.
- The container runs as an unprivileged user with a read-only filesystem.

## Without Docker

`RELAY_API_KEY=... DATA_DIR=./data PORT=8080 python3 relay.py` (Python 3.9+, standard library only),
behind any HTTPS reverse proxy. Set `TRUST_PROXY=1` only when a proxy sets `X-Forwarded-For`; `PROXY_HOPS` (default 1) is how many proxies are in front, counted from the right of that header, so an address the client writes itself is never believed.

## Tests

```bash
python3 -m unittest discover -s tests
```

## Managing members over HTTP (what Notch uses)

Host key only (anyone else gets 403): `GET /api/v1/members`, `POST /api/v1/members {"name", "access_hours"?, "quota_mb"?, ...}` (`access_hours`: how long their access lasts after they join, none = no end) →
`{"link"}` (an invitation), `POST /api/v1/members/NAME/invite` (a new key), `DELETE /api/v1/members/NAME` (revoke).
`GET /api/v1/ping` says `"owner": true` for the host key.
