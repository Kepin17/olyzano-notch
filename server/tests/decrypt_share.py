#!/usr/bin/env python3
"""Reference decryptor for the Notch share format (see src/services/ShareCrypto.h).

Usage: decrypt_share.py <encrypted file> <base64url key> <output file>
Prints the original file name. Needs the `cryptography` package; used by the C++ tests to
cross-check Notch's encryption against an independent implementation.
"""
import base64
import json
import struct
import sys

from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def decrypt(data, key):
    if data[:4] != b"NSH1":
        raise ValueError("not a Notch share")
    prefix = data[4:12]
    pos, index, name, out = 16, 0, "", bytearray()
    aes = AESGCM(key)
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        pos += 4
        sealed = data[pos:pos + length]
        pos += length
        plain = aes.decrypt(prefix + struct.pack(">I", index), sealed,
                            struct.pack(">IB", index, 1 if pos == len(data) else 0))
        if index == 0:
            name = json.loads(plain)["name"]
        else:
            out += plain
        index += 1
    return name, bytes(out)


if __name__ == "__main__":
    raw = open(sys.argv[1], "rb").read()
    k = sys.argv[2]
    name, body = decrypt(raw, base64.urlsafe_b64decode(k + "=" * (-len(k) % 4)))
    open(sys.argv[3], "wb").write(body)
    print(name)
