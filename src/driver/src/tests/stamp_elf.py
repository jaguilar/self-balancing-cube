#!/usr/bin/env python3
"""Stamps a linked firmware ELF with a deterministic 16-char SHA-256 buildstamp."""

import hashlib
import sys
from pathlib import Path

SENTINEL = b"BUILDSTAMP:0000000000000000\x00"


def main() -> int:
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.elf> <output.elf> <output.stamp>", file=sys.stderr)
        return 2

    in_elf = Path(sys.argv[1])
    out_elf = Path(sys.argv[2])
    out_stamp = Path(sys.argv[3])

    data = in_elf.read_bytes()
    count = data.count(SENTINEL)
    if count != 1:
        print(
            f"Expected exactly 1 buildstamp sentinel in {in_elf}, found {count}",
            file=sys.stderr,
        )
        return 1

    digest = hashlib.sha256(data).hexdigest()[:16]
    replacement = f"BUILDSTAMP:{digest}\x00".encode("ascii")
    assert len(replacement) == len(SENTINEL)

    out_elf.write_bytes(data.replace(SENTINEL, replacement, 1))
    out_stamp.write_text(f"{digest}\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
