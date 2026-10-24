#!/usr/bin/env python3
"""Reference decryptor for the Notch share format (see src/services/ShareCrypto.h).

Usage: decrypt_share.py <encrypted file> <base64url key> <output folder>
Writes every shared file into the folder and prints their names, one per line. Needs the
`cryptography` package; used by the C++ tests to cross-check Notch's encryption against an
independent implementation.
"""
import base64
import json
import os
import struct
import sys

from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def decrypt(data, key):
    """-> [(name, bytes)]"""
    if data[:4] != b"NSH1":
        raise ValueError("not a Notch share")
    prefix = data[4:12]
    (chunk,) = struct.unpack(">I", data[12:16])
    aes = AESGCM(key)
    pos, index = 16, 0

    def record():
        nonlocal pos, index
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        sealed = data[pos + 4:pos + 4 + length]
        pos += 4 + length
        plain = aes.decrypt(prefix + struct.pack(">I", index), sealed,
                            struct.pack(">IB", index, 1 if pos == len(data) else 0))
        index += 1
        return plain

    files = json.loads(record())["files"]
    out = []
    for f in files:
        body, left = bytearray(), f["size"]
        while left > 0:
            part = record()
            assert len(part) == min(chunk, left)
            body += part
            left -= len(part)
        out.append((f["name"], bytes(body)))
    assert pos == len(data), "trailing data"
    return out


if __name__ == "__main__":
    raw = open(sys.argv[1], "rb").read()
    k = sys.argv[2]
    os.makedirs(sys.argv[3], exist_ok=True)
    for name, body in decrypt(raw, base64.urlsafe_b64decode(k + "=" * (-len(k) % 4))):
        open(os.path.join(sys.argv[3], name), "wb").write(body)
        print(name)
