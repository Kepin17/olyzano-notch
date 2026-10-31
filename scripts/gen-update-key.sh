#!/usr/bin/env bash
# Makes the key pair that signs Olyzano Notch's updates (Ed25519), once.
#
#   scripts/gen-update-key.sh [private-key-file]
#
# - The PRIVATE key goes to the file you name (default ~/.config/notch-release/update-signing.pem, mode 600). It is never printed and
#   must never be committed. Put its text in the GitHub repository secret NOTCH_UPDATE_SIGNING_KEY (Settings -> Secrets and variables
#   -> Actions), or keep it offline and sign each release's SHA256SUMS-*.txt yourself (docs/RELEASING.md).
# - The PUBLIC key (64 hex characters) is written to packaging/update-pubkey.hex: commit that file. The program is built with it and
#   will only install an update whose checksum file was signed by the matching private key.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
key="${1:-$HOME/.config/notch-release/update-signing.pem}"
if [ -e "$key" ]; then
    echo "Refusing to overwrite $key (move it away first). A new key means every installed copy must be updated by hand once." >&2
    exit 1
fi
mkdir -p "$(dirname "$key")"
umask 077
openssl genpkey -algorithm ed25519 -out "$key"
# the raw public key is the last 32 bytes of the DER SubjectPublicKeyInfo
openssl pkey -in "$key" -pubout -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' \n' > "$here/packaging/update-pubkey.hex"
echo >> "$here/packaging/update-pubkey.hex"
echo "Private key:  $key   (secret: add it as the NOTCH_UPDATE_SIGNING_KEY secret, then keep a backup somewhere safe)"
echo "Public key:   $here/packaging/update-pubkey.hex   (commit this one)"
