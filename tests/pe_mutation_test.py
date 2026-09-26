# SPDX-License-Identifier: GPL-3.0-only
"""Reject malformed copies of the actual compiled image; never execute it."""
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from check_pe import check_bytes

original = Path(sys.argv[1]).read_bytes()
check_bytes(original)
pe = struct.unpack_from('<I', original, 0x3c)[0]
opt = pe + 24
sec = opt + struct.unpack_from('<H', original, pe + 20)[0]
cases = {'empty': b'', 'truncated DOS': original[:50], 'truncated sections': original[:sec + 8]}
for title, off, fmt, value in [
    ('wrong machine', pe + 4, 'H', 0x8664),
    ('wrong optional magic', opt, 'H', 0x10b),
    ('wrong subsystem', opt + 68, 'H', 3),
    ('missing protections', opt + 70, 'H', 0),
    ('invalid entrypoint', opt + 16, 'I', 0xffffffff),
    ('RWX code', sec + 36, 'I', 0xe0000020),
    ('invalid import RVA', opt + 120, 'I', 0xffffffff),
    ('truncated import descriptor', opt + 124, 'I', 19),
    ('missing descriptor terminator', opt + 124, 'I', 20),
    ('oversized section count', pe + 6, 'H', 65535),
]:
    image = bytearray(original)
    struct.pack_into('<' + fmt, image, off, value)
    cases[title] = bytes(image)
for title, image in cases.items():
    try:
        check_bytes(image)
    except ValueError:
        continue
    raise AssertionError(f'Accepted malformed PE: {title}')
print(f'PASS: {len(cases)} malformed-PE regression cases rejected')
