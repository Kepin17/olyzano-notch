# Releasing, and how updates are trusted

Notch can install an update by itself (Settings → About → *Update now*). Because that runs a downloaded installer, the app does not
trust the release page alone: each release's checksum file is **signed**, and the program only installs a file whose checksum is listed
in a checksum file with a good signature.

## How it fits together
- `SHA256SUMS-linux.txt` / `SHA256SUMS-windows.txt` list the SHA-256 of the packages (made by the workflows).
- `SHA256SUMS-<system>.txt.sig` is an Ed25519 signature of that exact file (base64 text).
- The matching **public key** is built into the program from `packaging/update-pubkey.hex` (64 hex characters, committed).
- *Update now* downloads the checksum file and its `.sig`, checks the signature, then downloads the package, checks its SHA-256, and only
  then installs. A release without a `.sig`, or with a wrong one, is never installed by the app: the *Download* button still works.

## One-time setup
1. `scripts/gen-update-key.sh` makes the key pair. The private key is written to `~/.config/notch-release/update-signing.pem`
   (never printed, never committed); the public key goes to `packaging/update-pubkey.hex`.
2. Commit `packaging/update-pubkey.hex`.
3. Add the private key's text as the repository secret **`NOTCH_UPDATE_SIGNING_KEY`** (Settings → Secrets and variables → Actions).
   From then on the Linux and Windows workflows sign the checksum files and attach the `.sig` files to the release.
4. Keep a backup of the private key somewhere safe and offline.

Releases built before step 3 (or without the secret) are not signed: the workflows print a warning and the app tells the person to use
*Download*. Copies of Notch older than the first signed release do not check signatures; they update by hand once.

## Stronger: sign on your own computer
A key that lives only on your machine cannot leak through GitHub. Skip the secret, build the release as usual, then:

```bash
gh release download vX.Y.Z -p 'SHA256SUMS-*.txt'
for f in SHA256SUMS-*.txt; do
  openssl pkeyutl -sign -rawin -inkey ~/.config/notch-release/update-signing.pem -in "$f" | base64 -w0 > "$f.sig"
done
gh release upload vX.Y.Z SHA256SUMS-*.txt.sig
```

## If the key is lost or leaks
Generate a new pair (move the old key file away first), commit the new `packaging/update-pubkey.hex`, update the secret, and publish a
release. People on older copies must install that one release by hand (the old key no longer matches), then updates work again.
