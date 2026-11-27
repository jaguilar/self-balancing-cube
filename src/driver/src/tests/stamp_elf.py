#!/usr/bin/env python3
"""Stamps a linked firmware ELF with a deterministic 16-char SHA-256 buildstamp."""

import hashlib
import struct
import sys
from pathlib import Path

SENTINEL = b"BUILDSTAMP:0000000000000000\x00"
PT_LOAD = 1


def find_loadable_sentinels(data: bytes) -> list[int]:
    """Return file offsets of SENTINEL within ELF PT_LOAD segments."""
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise ValueError("Not a valid ELF file")

    ei_class = data[4]
    ei_data = data[5]
    endian = "<" if ei_data == 1 else ">" if ei_data == 2 else None
    if endian is None:
        raise ValueError(f"Unsupported ELF endianness: {ei_data}")

    if ei_class == 1:  # ELF32
        (e_phoff,) = struct.unpack_from(f"{endian}I", data, 28)
        e_phentsize, e_phnum = struct.unpack_from(f"{endian}HH", data, 42)
        phdr_fmt = f"{endian}IIIII"  # p_type, p_offset, p_vaddr, p_paddr, p_filesz
    elif ei_class == 2:  # ELF64
        (e_phoff,) = struct.unpack_from(f"{endian}Q", data, 32)
        e_phentsize, e_phnum = struct.unpack_from(f"{endian}HH", data, 54)
        phdr_fmt = f"{endian}IIQQQQ"  # p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz
    else:
        raise ValueError(f"Unsupported ELF class: {ei_class}")

    offsets: list[int] = []
    for i in range(e_phnum):
        ph_start = e_phoff + i * e_phentsize
        if ei_class == 1:
            p_type, p_offset, _, _, p_filesz = struct.unpack_from(phdr_fmt, data, ph_start)
        else:
            p_type, _, p_offset, _, _, p_filesz = struct.unpack_from(phdr_fmt, data, ph_start)
        if p_type != PT_LOAD or p_filesz == 0:
            continue
        seg_end = p_offset + p_filesz
        pos = p_offset
        while True:
            idx = data.find(SENTINEL, pos, seg_end)
            if idx == -1:
                break
            offsets.append(idx)
            pos = idx + 1
    return offsets


def main() -> int:
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.elf> <output.elf> <output.stamp>", file=sys.stderr)
        return 2

    in_elf = Path(sys.argv[1])
    out_elf = Path(sys.argv[2])
    out_stamp = Path(sys.argv[3])

    data = in_elf.read_bytes()
    offsets = find_loadable_sentinels(data)
    if len(offsets) != 1:
        print(
            f"Expected exactly 1 buildstamp sentinel in PT_LOAD segments of {in_elf}, "
            f"found {len(offsets)}",
            file=sys.stderr,
        )
        return 1

    digest = hashlib.sha256(data).hexdigest()[:16]
    replacement = f"BUILDSTAMP:{digest}\x00".encode("ascii")
    assert len(replacement) == len(SENTINEL)

    offset = offsets[0]
    patched = bytearray(data)
    patched[offset : offset + len(SENTINEL)] = replacement
    out_elf.write_bytes(bytes(patched))
    out_stamp.write_text(f"{digest}\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
