#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Static regression for the 0.1.14 desktop-latency hot paths."""

from pathlib import Path

present = Path("driver/present.c").read_text(encoding="utf-8")
vsync = Path("driver/vsync.c").read_text(encoding="utf-8")
adapter = Path("driver/adapter.c").read_text(encoding="utf-8")

# The write-combined framebuffer is memory, not a register bank. A full 1080p
# update must not execute a register-buffer operation (and its ordering
# semantics) once per scan line.
flush_start = present.index("VOID RpFlush")
blank_start = present.index("VOID RpBlank")
flush = present[flush_start:blank_start]
assert "WRITE_REGISTER_BUFFER_ULONG" not in flush
assert "RtlCopyMemory" in flush
assert "rowBytes" in flush
assert flush.count("KeMemoryBarrier();") == 1

# Blank uses the same memory-write model rather than one volatile ULONG store
# per pixel.
rect_start = present.index("static RP_RECT RpRect")
blank = present[blank_start:rect_start]
assert "RtlZeroMemory" in blank
assert "volatile ULONG" not in blank
assert blank.count("KeMemoryBarrier();") == 1

# Keep explicit timing evidence for the next hardware run.
assert "PresentMaxUs" in present
assert "PresentOver16ms" in present
assert "maxPresentUs=" in adapter

# Successful Windows VSync-control chatter must not write dozens of registry
# values on every enable/disable transition. Preserve first/failure evidence
# plus a periodic snapshot instead.
assert "#define RP_VSYNC_CONTROL_PERSIST_MASK   63ULL" in vsync
assert "static BOOLEAN RpShouldPersistControlResult" in vsync
record = vsync[vsync.index("static VOID RpRecordControlInterruptResult"):
               vsync.index("BOOLEAN RpVSyncRegistrationAvailable")]
gate = record.index("RpShouldPersistControlResult")
counters = record.index("RpRecordVSyncCounters(a);")
assert gate < counters

# Do not "optimize" away the DPC path: the Microsoft KMDOD interrupt pattern
# still notifies dxgkrnl and queues a DPC.
isr = vsync[vsync.index("BOOLEAN NTAPI RpInterrupt"):]
assert "DxgkCbNotifyInterrupt" in isr
assert "DxgkCbQueueDpc" in isr
assert "DxgkCbNotifyDpc" in adapter

print("PASS: 0.1.14 removes framebuffer/register hot-path abuse and rate-limits diagnostic registry writes")
