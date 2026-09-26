# SPDX-License-Identifier: GPL-3.0-only
"""Bounded validation of the linked kernel PE. Not a proof of runtime correctness."""
from __future__ import annotations
import pathlib
import struct
import sys


def check_bytes(data: bytes) -> list[str]:
    def span(offset: int, length: int) -> None:
        if offset < 0 or length < 0 or offset > len(data) - length:
            raise ValueError('Truncated or out-of-range PE data')
    def u16(offset: int) -> int:
        span(offset, 2)
        return struct.unpack_from('<H', data, offset)[0]
    def u32(offset: int) -> int:
        span(offset, 4)
        return struct.unpack_from('<I', data, offset)[0]
    span(0, 64)
    if data[:2] != b'MZ':
        raise ValueError('Not a PE image')
    pe = u32(0x3c)
    span(pe, 24)
    if data[pe:pe + 4] != b'PE\0\0' or u16(pe + 4) != 0xaa64:
        raise ValueError('Expected a native ARM64 PE signature/machine')
    count, opt_size = u16(pe + 6), u16(pe + 20)
    if not 1 <= count <= 96 or opt_size < 240:
        raise ValueError('Invalid section count or truncated PE32+ optional header')
    opt = pe + 24
    span(opt, opt_size)
    if u16(opt) != 0x20b or u16(opt + 68) != 1:
        raise ValueError('Expected PE32+ native subsystem')
    if u16(opt + 70) & 0x1c0 != 0x1c0:
        raise ValueError('Missing NX, ASLR or force-integrity flag')
    if u32(opt + 108) < 2:
        raise ValueError('Missing import data directory')
    sec = opt + opt_size
    span(sec, count * 40)
    sections = []
    for i in range(count):
        off = sec + i * 40
        flags = u32(off + 36)
        if flags & 0xa0000000 == 0xa0000000:
            raise ValueError('Writable executable section')
        va, raw_size, raw_offset = u32(off + 12), u32(off + 16), u32(off + 20)
        span(raw_offset, raw_size)
        sections.append((va, raw_size, raw_offset, flags))
    def raw(rva: int, length: int = 1) -> int:
        # Only file-backed bytes can contain PE descriptors. Virtual zero-fill is not data.
        hits = [(offset + rva - va) for va, size, offset, _ in sections
                if rva >= va and length <= size and rva - va <= size - length]
        if len(hits) != 1:
            raise ValueError('Unmapped/ambiguous or non-file-backed RVA')
        span(hits[0], length)
        return hits[0]
    entry = u32(opt + 16)
    if not any(va <= entry < va + size and flags & 0x20000000 for va, size, _, flags in sections):
        raise ValueError('Entrypoint is not in file-backed executable code')
    imp, length = u32(opt + 120), u32(opt + 124)
    if not imp or not 20 <= length <= 65536:
        raise ValueError('Missing or unreasonable import directory')
    start = raw(imp, length)
    imports = []
    terminated = False
    for off in range(start, start + length - 19, 20):
        if data[off:off + 20] == b'\0' * 20:
            terminated = True
            break
        name_rva = u32(off + 12)
        name_bytes = bytearray()
        for pos in range(256):
            char = data[raw(name_rva + pos)]
            if char == 0:
                break
            name_bytes.append(char)
        else:
            raise ValueError('Unterminated import name')
        try:
            name = bytes(name_bytes).decode('ascii').lower()
        except UnicodeDecodeError as error:
            raise ValueError('Non-ASCII import module') from error
        if name not in {'ntoskrnl.exe', 'hal.dll'}:
            raise ValueError(f'Unexpected kernel import module: {name!r}')
        imports.append(name)
    if not terminated or 'ntoskrnl.exe' not in imports:
        raise ValueError('Unterminated/empty kernel import descriptor table')
    return imports


def check(path: pathlib.Path) -> None:
    imports = check_bytes(path.read_bytes())
    print(f'PASS: bounded ARM64 PE; native/NX/ASLR/integrity; no RWX; imports={imports}')


if __name__ == '__main__':
    try:
        check(pathlib.Path(sys.argv[1]))
    except (ValueError, OSError, struct.error, IndexError) as error:
        print(f'PE validation FAILED: {error}', file=sys.stderr)
        sys.exit(1)
