# Security Audit — Olyzano Notch (xiaomi-model)

Date: 2026-10-28 · Scope: full project (documentation, `src/`, `server/`, `docs/app/`, `docs/index.html`, CI, packaging, tools)
Method: documentation review + manual code review + 3 delegated deep audits (relay server, web client, C++ file/exec surface) + targeted verification of every finding against source. The relay findings marked *confirmed* were reproduced with live PoCs against a throwaway instance; no project files were modified.

Test status: relay `python3 -m unittest discover -s tests` → **30/30 pass**. `tst_core` → **103/104 pass** (the one failure, `vaultLifecycle`, is environmental: this sandbox has a read-only `/run/user/1000`, so the gocryptfs mount dir cannot be created — not a code defect).

---

## 1. Architecture (from the documentation)

| Layer | What | Attack surface |
|---|---|---|
| `src/` Qt/C++/QML desktop app | shelf, tools, dock, share, vault, nearby, assistant | local files, LAN (ShareServer TCP, Nearby UDP/TCP), GitHub (updates), HuggingFace (whisper model), lrclib (lyrics) |
| `server/relay.py` + `page.html`/`join.html` | self-hosted E2E-encrypted file relay (Docker + Caddy) | public internet |
| `docs/app/` (plain ES modules, PWA) | in-browser shelf + tools + AES-256 vault | browser origin, CDN (tesseract via jsDelivr) |
| `docs/index.html` | install/landing page (GitHub Pages) | GitHub API data |

Trust claims worth testing (all verified true unless noted below): E2E encryption where the key stays in the link `#fragment`; locked categories encrypted at rest; "the only network call by default is a once-a-day update check"; PIN/password protections; single-use shares and invites.

---

## 2. HIGH

### H1 — Web app: same-origin script execution by opening HTML/SVG shelf files
`docs/app/js/shelf.js:56-67` opens shelf items as documents via `window.open(blobURL, "_blank", "noopener")` for kinds `image/pdf/text/...`; `docs/app/js/util.js:77-86` classifies `text/html`, `*.html`, and `image/svg+xml` (via `t.startsWith("image/")`) into those kinds. **blob: URLs inherit the app origin**, so a crafted `evil.html`/`evil.svg` on the shelf — delivered by drag-drop, paste, the PWA share-target, the unzip tool, or a QR share — executes `<script>`/`<svg onload>` in the app's origin on one click. Impact: full read of IndexedDB `olyzano-notch-web` (all shelf files, locked-category ciphertext + salt + password verifiers → offline cracking), localStorage, and the ability to exfiltrate/modify shelf content. `noopener` blocks only `window.opener`.
**Fix:** never open active content as a document — render text via `textContent`/sandboxed `<iframe sandbox>` (no `allow-same-origin`), force download for html/svg/xhtml, or serve blobs with `Content-Disposition: attachment` + `nosniff`.

### H2 — Web app: vendored pdf.js 3.11.174 is vulnerable to CVE-2024-4367
`docs/app/vendor/pdf.min.js` (pinned in `docs/app/vendor/LICENSES.md`) is pdfjs-dist **3.11.174**; CVE-2024-4367 (GHSA-wgrm-whmg-jj86, fixed in 4.2.67) gives arbitrary JS in the viewer context from a crafted font matrix in a PDF. Render paths: `docs/app/js/tools/pdf.js:6-10,73-94` (render/compress) and `tools/ocr.js:14-21`. A malicious PDF processed with *Compress PDF* or *OCR* → JS at app origin → same impact as H1.
**Fix:** upgrade to pdfjs-dist ≥ 4.2.67 (current 5.x preferred).

### H3 — Relay: HTTP request smuggling / response-queue desync via unconsumed request bodies *(confirmed PoC)*
`server/relay.py:535` (`join()`), `server/relay.py:569` (`members_api` POST) read only `min(Content-Length, 4096)` of the body and never drain the rest nor set `close_connection` (HTTP/1.1 persistent by default, `relay.py:305`). Leftover body bytes are parsed as the **next** request on the connection. PoC: one `POST /api/v1/join` with `Content-Length: 6000` whose tail was a complete `GET /api/v1/ping` request → the relay executed both and returned two responses on one connection (log: `POST 404` + `GET 401`). Behind the shipped Caddy (which pools upstream connections) this desyncs the response queue: a victim's next request can receive the phantom response and a victim's real response (e.g. a `/api/v1/join` 200 carrying a fresh member key) can leak to whoever next uses that pooled connection.
**Fix:** read exactly `Content-Length` bytes (reject > limit with 413 + `close_connection=True`), or set `close_connection=True` whenever a body remains partially unread.

---

## 3. MEDIUM

### M1 — Relay: single-use invite tokens are not single-use under concurrency *(confirmed PoC)*
`server/relay.py:171-191` (`redeem`): `SELECT … used=0` then `UPDATE used=1` are separate statements; `self.lock` guards only the failure counter. PoC: 8 concurrent redemptions of one token → **5× HTTP 200** (5 keys issued). `key_hash` is last-writer-wins, so a racing attacker can end up holding the only valid key while the victim's returned key silently dies. Mitigated by 192-bit `secrets.token_urlsafe(24)` tokens (guessing infeasible); matters for shared/leaked links.
**Fix:** atomic `UPDATE invites SET used=1 WHERE token_hash=? AND used=0` with rowcount check inside `BEGIN IMMEDIATE`.

### M2 — Relay: quota / `MAX_TOTAL_MB` / `max_active` enforcement is racy
`server/relay.py:492-497` checks `used()`/`used()+length`, then writes at `:503-516` with no lock in between. N parallel uploads each see `used=0` and overshoot member quota and the global `MAX_TOTAL_MB` by ~N×. Authenticated members only.
**Fix:** reserve quota atomically (under lock or a DB row) before writing; count staged bytes.

### M3 — Relay: PIN/invite lockouts bypassable via spoofed `X-Forwarded-For` when `TRUST_PROXY=1`
`server/relay.py:317-322` feeds both lockouts. Unset → `client_address` (not spoofable, good). With `TRUST_PROXY=1` (as `docker-compose.yml:11` ships it) the `split(",")[-1]` convention is correct for Caddy (which overwrites XFF), but if the relay port is ever published directly or fronted by a proxy that forwards client XFF verbatim, an attacker rotates XFF per request for unlimited PIN guesses (`/s/<id>/blob`) and invite-token guesses (`/api/v1/join`). As shipped (relay unpublished, Caddy front) risk is Low; config footgun → Medium.
**Fix:** trust XFF only when the direct peer is a configured trusted proxy; document the requirement.

### M4 — Web app: service worker `/share-target` accepts cross-origin POSTs (CSRF file injection)
`docs/app/sw.js:11-21` handles `POST …/share-target` with no `Origin`/`Sec-Fetch-Site` check: any web page can auto-submit a multipart form and inject files onto the shelf (`main.js:163-167` ingests them). Chained with H1 this becomes remote → one-click code execution; alone it silently pollutes shelf content.
**Fix:** reject cross-origin POSTs (check `Origin`/`Sec-Fetch-Site`) or require a per-install nonce in the action URL.

### M5 — Zip-slip via unsanitized entry names (web) + partial unzip sanitization
`server/page.html:373-397` (`makeZip`) and `docs/app/js/tools/zip.js:7-17` write `e.name` verbatim into browser-built zips; names come from the decrypted share manifest (`page.html:612`) or shelf records — a malicious sender can name a file `../../../evil.sh`, `C:\evil.exe`, so "Download zip" produces a zip that escapes the target dir on vulnerable desktop extractors. `zip.js:19-24` (unzip) strips only `/`-separated dirs; `..\..\x`, bare `..`, drive prefixes survive onto the shelf and can be re-exported. No entry-count/expansion-size cap on `fflate.unzipSync` (zip-bomb tab DoS).
**Fix:** normalize `\`→`/`, drop drive prefixes/leading slashes, flatten/reject `..`, dedupe; bound total uncompressed size and entry count.

### M6 — OCR pulls executable code from jsDelivr at runtime (supply chain + usage leak)
`docs/app/js/tools/ocr.js:8-11` uses tesseract.js defaults → worker/wasm/language data fetched from `cdn.jsdelivr.net` at first OCR. A compromised CDN artifact executes at app origin (same impact as H1); requests leak language-usage metadata; `sw.js:28-31` caches it cache-first so a one-time poison persists.
**Fix:** self-host `tesseract-core.wasm.js` + worker + `.traineddata` under `vendor/`, or pin + verify digests; disclose the dependency in the "nothing leaves your device" copy.

### M7 — C++/desktop: path traversal in tool output names (extract "pages" option)
`src/services/DocTools.cpp:261-267`: `out(in.base + " (pages " + option.simplified() + ").pdf")` — `option` is the free-text page-range field from QML (`Main.qml:3021` → `runTool("extract", …)`). Only whitespace is simplified; a `/` escapes the per-job scratch dir (`scratchDir()/tools/<uuid>`, `NotchController.cpp:1442`). The external tool writes at the escaped path and `adoptFile` renames/copies it in — across filesystems the copy leaves the original at the escaped location. Same class: `convert`'s `format = option` (`DocTools.cpp:271-282`) reaches `out(in.base + '.' + format)`.
**Fix:** route every derived name through `FileService::safeFileName` (or reject `/`, `\`, control chars); build the label from the *parsed* page list (already validated) instead of the raw text; assert canonical-path containment before writing.

### M8 — C++/desktop: category name with `/` → zip written outside managed storage
`src/app/NotchController.cpp:915-929` (`createCategory`/`renameCategory`): `name.simplified().left(60)` keeps `/`. `zipItems` (`:1705-1745`) builds `newItem("%1 (%2 %3).zip")` from the category display name and renames the archive to `pathOf(zip)` = `filesDir/uuid/<originalName>` **without** the `isValidStoredName` check used in `renameItem` (`:1646`). A category named `../../…/.config/autostart/x` makes the final rename land outside the storage dir (file creation of zip content — planting/DoS; `QFile::rename` won't overwrite).
**Fix:** reject/strip `/` and `\` in `createCategory`/`renameCategory`; enforce `isValidStoredName` before any rename.

### M9 — C++/desktop: FolderWatcher follows symlinks → arbitrary file contents auto-imported
`src/services/FolderWatcher.cpp:26-33,45-61` lists `QDir::Files|QDir::Hidden` without `QDir::NoSymLinks`; `FileService.cpp:33-46` (`validateSource`) and `copyInto` follow links. Watched: `~/Downloads`, the screenshot dir, KDE Connect receives. PoC: `ln -s ~/.ssh/id_rsa ~/Downloads/leak.png` → the target's bytes are copied into the shelf as "leak.png" (preview/copy/share-able). Requires write access to a watched folder (shared machines, other local users).
**Fix:** skip symlinks (`QDir::NoSymLinks` or `lstat`/`isSymLink` check) for auto-add.

### M10 — Update chain: SHA-256 enforced but co-hosted; root install on Linux
`src/platform/Updates.cpp:298-387`: HTTPS with default cert validation, `NoLessSafeRedirectPolicy`, `trustedHost` re-checked after redirects, SHA-256 mismatch deletes the file, no checksum file ⇒ refuse install — all good. But `SHA256SUMS-*.txt` is an asset of the **same** GitHub release (no signature/pinning): anyone who can publish the release (account/CI compromise) or any CA-trusted party (corporate TLS inspection, malware root) can serve a consistent fake set and get code execution — `pkexec apt-get install -y <deb>` (maintainer scripts as **root** behind a generic polkit prompt) on Linux, silent elevated Inno `/SILENT` on Windows. `--check-updates --install-update` (`main.cpp:170-187`) installs with no UI confirmation.
**Fix:** sign releases (minisign/GPG) and pin the verification key in the binary; keep the current https+host+hash checks.

---

## 4. LOW

| # | Finding | Where | Fix |
|---|---|---|---|
| L1 | QML `Text` default **AutoText** interprets HTML in file/category/device names and error messages (UI spoofing + remote image fetch from a mere filename; no JS/anchors without `linkActivated`) — e.g. `Main.qml:2146, 2195, 2841, 3220, 2952` | src/qml | `textFormat: Text.PlainText` on every Text showing names/errors |
| L2 | `safeFileName` gaps: `\`, C0 control chars, Windows reserved names not stripped (`FileService.cpp:105-113`); `downloadName` percent-decodes `%5C`→`\`; `addLocalFile`/`newItem` bypass it entirely (`NotchController.cpp:404`) → traversal on Windows via `uuid\..\..\x` storedName | src/services, src/app | extend sanitizer; call it in `addLocalFile`/`newItem` |
| L3 | Web vault KDF PBKDF2-SHA256 **250k** iterations (`docs/app/js/crypto.js:8-12`) vs 600k in `page.html:325`; salt+verifier live in IndexedDB → offline brute-force of weak vault passwords | docs/app | raise to ≥ 600k (or Argon2/scrypt); re-wrap on unlock |
| L4 | Web vault writes **plaintext blob** to IndexedDB before re-encrypting (`docs/app/js/vault.js:26-52`) — crash between the two puts leaves plaintext durable | docs/app | encrypt before first `put` |
| L5 | Relay: uncaught `ValueError` on huge Range digits (`relay.py:414-415`) — *confirmed*: `Range: bytes=0-` + 5000 nines kills the connection thread with a traceback | server | cap digit length / catch → 416 |
| L6 | Relay: single-use download refundable — abort a counted transfer to trigger `release()` (`relay.py:449-450`); with Range counting only `end==size-1` (`:422`) the tail can be collected across aborted retries without consuming the link | server | cap refunds per share |
| L7 | Relay: unbounded rate-limit dicts (`relay.py:88, 203` never prune) + thread-per-connection slowloris surface (`:610`) | server | prune periodically; cap connections |
| L8 | Relay: SQLite connections never closed (`relay.py:91-93`; visible `ResourceWarning`s in tests) | server | `contextlib.closing` / explicit `close()` |
| L9 | Relay: PBKDF2 PIN check runs under the global `Store.lock` (`relay.py:287-298`) → wrong-PIN requests throttle all share accounting (~100 ms each) | server | compute hash outside the lock |
| L10 | Relay: `store.used()`/`active()` full directory scans per request (`relay.py:234-247`) — I/O amplification for authenticated members | server | running total in the DB |
| L11 | Web SW cache: unanchored `/jsdelivr|unpkg/` host regex matches `evil-unpkg.com` etc.; cache-first for all CDN fetches (`sw.js:24-31`) | docs/app | exact-host allowlist + stale-while-revalidate |
| L12 | Web zip/unzip: no zip-bomb limits (`zip.js:22`) | docs/app | bound expansion |
| L13 | Desktop `.desktop` link items (`Type=Link`, `URL=`) opened via `QDesktopServices`; a dropped crafted `Type=Application` `.desktop` delegates to the DE launcher (DE-dependent trust) | `FileService.cpp:172-180`, `NotchController.cpp:563-570` | store links as `.url`/note; restrict `URL=` to http(s) |
| L14 | Debug CLI flags `--shot*`, `--dump-discord` overwrite arbitrary argv paths (`main.cpp:551-642, 193-208`) | src/main.cpp | gate behind env/debug build; refuse overwrites |
| L15 | Discord OAuth2 **client secret compiled into shipped builds** (release CI passes it to CMake) — extractable from any binary; documented as low-trust but it is a live credential for the maintainer's Discord app | `.github/workflows/windows.yml:42-53` | treat as public (Discord allows); rotate on leak; prefer PKCE-style public client if Discord supports it |
| L16 | Build supply chain: `FetchContent` pins whisper.cpp to the **mutable tag** `v1.9.5` (no commit hash) (`CMakeLists.txt:267`) | CMakeLists.txt | pin `GIT_TAG` to a commit SHA + verify |
| L17 | Nearby: receiver rejects legit transfers larger than ~50 GB ("Unexpected size" — encrypted overhead `20 B/MB` exceeds the `1 MB + 64 B/file` slack at `NearbyService.cpp:321`); conversely a sender can advertise a huge `m_total` and stream until the disk fills after the user accepts (only a `lowDisk` warning) | src/services | size bound = `m_total * 1.01 + slack`; refuse absurd totals |
| L18 | Relay: empty/missing `X-Pin` counts as a failed attempt (`relay.py:402`) → bots/prefetches burn lockout budget | server | reject empty PINs before counting |
| L19 | Docs `tests.html:16` interpolates `JSON.stringify(...)` into `innerHTML` (dev-only today) | docs/app | `textContent` |

---

## 5. INFO / design notes

- **"This WiFi" shares are plain HTTP by design** (`ShareServer.cpp`): the 128-bit per-share token (OS CSPRNG) is the only gate on an untrusted network; anyone who sniffs the QR/link traffic reads the files and the PIN (sent in `X-Pin`). Consider a warning when the active network is not a private profile.
- **E2E keys at rest:** active relay share links (including the `#fragment` key) are persisted in `~/.config/notch/sessions.json`, owner-only (0600) — deliberate ("the links carry the keys: owner-only") but worth stating: E2E does not protect against local disk access/backups/malware running as the user. The same applies to the Windows vault's decrypted view in `%TEMP%\notch-vault-…` while unlocked (documented).
- **No compiler hardening flags** in `CMakeLists.txt` (no explicit `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, PIE/RELRO hardening) — distro gcc defaults cover some; add them explicitly.
- **`docs/index.html` (install page)** uses `innerHTML` only with static literals (`label(el, text, a)`, line 983) and GitHub API data into `textContent`/`href` — no XSS found. `docs/app/index.html` has **no CSP** (GitHub Pages limits header control, but a `<meta http-equiv="Content-Security-Policy">` would contain H1-class bugs).
- **Relay hardening leftovers:** crash between blob `os.replace` and `save_meta` (`relay.py:516/524`) leaves unsweepable dirs outside quota; `meta()` raises uncaught `KeyError` on malformed meta (`:220`); `DELETE /api/v1/shares/<id>` returns 200 even for non-owners (no deletion — no oracle, but misleading); Caddyfile comment vs `MAX_FILE_MB` off-by-comment; `int(float(env))` crashes on inf/nan at startup.
- **QR link opener** (`docs/app/js/share.js:28`) navigates to any http(s) URL decoded from a scanned QR (scheme allowlist blocks `javascript:`/`data:`) — acceptable, consider a confirmation for non-LAN hosts.
- **Functional note:** `tests/tst_core.cpp:2314` `vaultLifecycle` fails in environments where `XDG_RUNTIME_DIR` is read-only (sandbox artifact, not a defect).

---

## 6. What is done well (verified)

**Crypto:** AES-256-GCM shares with random 8-byte nonce prefix + per-record u32 index, AAD = index ‖ last-record flag → reorder/substitution/truncation all fail the tag; every chunk authenticated before assembly (C++ and JS agree); password wrap = PBKDF2-SHA256 **600k**, random salt+IV, AAD "notch-key", no IV reuse; key in `#fragment` never sent to any server; WebCrypto keys non-extractable. Nearby: X25519 with **commitment-before-reveal** + 4-digit SAS transcript binding (a MITM cannot make both screens agree), accept-proof MAC, stream key bound to the file list. Vault: gocryptfs on Linux; AES-256-GCM + HMAC-derived cipher names on Windows; password on stdin (not argv); keys zeroed.

**Server (relay):** `hmac.compare_digest` for host key; 256-bit member keys stored only as SHA-256 hashes; 192-bit hashed invite tokens; 128-bit share IDs; PIN PBKDF2-100k + per-(IP,share) lockout (5 tries/10 min), constant-time compare; single-use **download** counting atomic (8-way race test passes); no `eval`/`exec`/`pickle`/`yaml`/`subprocess`; no SSRF; no server-side archive handling; IDs regex-validated (no path traversal possible); parameterized SQL; logs are method+status only; identical 404 for missing/not-owned shares (no oracle); CSP + `no-store`/`nosniff`/`no-referrer`; Docker: non-root, read-only rootfs, `cap_drop: ALL`, `no-new-privileges`, relay port unpublished.

**Desktop:** no shell invocation anywhere (QProcess program+argv throughout; the two `/bin/sh -c` uses are fixed literals); tool timeouts + kill; gs output path absolute-prefixed (no option injection); deletion hardening (UUID pattern, symlink-aware, canonical-path containment); `expandFolder` skips hidden entries and never follows dir symlinks; `addUrls`/paste scheme allowlist (http/https/data); update flow enforces checksum + trusted host + no-less-safe redirects; whisper model pinned by SHA-256 **and** size; assistant URLs restricted to an https whitelist/regex (no `file:`/`javascript:`); `--assistant-say` is parse-only; search/preview reads size-bounded and rendered as PlainText in the search pane; parameterized SQL everywhere (no injection).

**Web:** all dynamic text rendered via `textContent`/`h()` helpers (no `innerHTML` with untrusted data in app code); share-data JSON `<`-escaped server-side before embedding; strict CSP on both share surfaces; PIN only in `X-Pin` headers, never URLs; vault keys memory-only; files saved via `download` attribute.

**CI:** no script-injection sinks (version extracted via `[0-9.]+` regex; `inputs.release_tag` only flows into action inputs); secrets passed via env, `.env` gitignored.

---

## 7. Priority order

1. **H1, H2** (web app code execution) — upgrade pdf.js; stop opening active content as documents.
2. **H3** (relay desync) + **M1** (invite race) — both confirmed; small patches.
3. **M4** (share-target CSRF) + **M5** (zip-slip) — small patches, high leverage.
4. **M7, M8, M9** (desktop path traversal + symlink import) — local attackers / shared machines.
5. **M10** (signed updates) — trust-model decision.
6. **M2, M3, M6** then the LOW list (L1 AutoText and L3 KDF are quick wins).
