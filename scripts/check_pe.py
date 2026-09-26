# SPDX-License-Identifier: GPL-3.0-only
"""Validate the actual linked PE, not its filename. Uses only the Python standard library."""
import pathlib
import struct
import sys


def check(path: pathlib.Path) -> None:
    data = path.read_bytes()
    def u16(o): return struct.unpack_from('<H', data, o)[0]
    def u32(o): return struct.unpack_from('<I', data, o)[0]
    if data[:2] != b'MZ': raise ValueError('Not a PE image')
    pe = u32(0x3c)
    if data[pe:pe + 4] != b'PE\0\0': raise ValueError('Bad PE signature')
    if u16(pe + 4) != 0xaa64: raise ValueError('Image is not native ARM64')
    opt = pe + 24
    if u16(opt) != 0x20b or u16(opt + 68) != 1: raise ValueError('Expected PE32+ native subsystem')
    if u16(opt + 70) & 0x1c0 != 0x1c0: raise ValueError('Missing NX, ASLR, or force-integrity flag')
    sections = []
    sec = opt + u16(pe + 20)
    for i in range(u16(pe + 6)):
        o = sec + i * 40
        flags = u32(o + 36)
        if flags & 0xa0000000 == 0xa0000000: raise ValueError('Writable executable section')
        sections.append((u32(o + 12), max(u32(o + 8), u32(o + 16)), u32(o + 20)))
    def raw(rva):
        for va, size, off in sections:
            if va <= rva < va + size: return off + rva - va
        raise ValueError(f'Unmapped RVA {rva:x}')
    imports = []
    imp = u32(opt + 112 + 8)
    if imp:
        o = raw(imp)
        while any(data[o:o + 20]):
            n = raw(u32(o + 12)); end = data.index(b'\0', n)
            imports.append(data[n:end].decode('ascii').lower()); o += 20
    if not imports or set(imports) - {'ntoskrnl.exe', 'hal.dll'}:
        raise ValueError(f'Unexpected kernel imports: {imports}')
    print(f'PASS: ARM64 native image; NX/ASLR/integrity; no RWX; imports={imports}')


if __name__ == '__main__':
    try:
        check(pathlib.Path(sys.argv[1]))
    except (ValueError, OSError, struct.error, IndexError) as exc:
        print(f'PE validation FAILED: {exc}', file=sys.stderr)
        sys.exit(1)
