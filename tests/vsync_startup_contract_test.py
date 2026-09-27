#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Static regression for the first-Windows-enabled VSync anchor contract."""

from pathlib import Path

source = Path("driver/vsync.c").read_text(encoding="utf-8")

assert "RpPrimeVSyncAnchor" not in source
assert "STATUS_IO_TIMEOUT" not in source
assert "hardware ready without startup anchor" in source

# StartDevice must seed no fake phase.
assert "InterlockedExchange64(&a->LastVSyncQpc, 0);" in source

# Enabling CRTC_VSYNC must reset any old phase, clear stale status, then enable
# the real PixelValve VFP-start source.
enable = source.index("if (control->Enable)")
enable_block = source[enable:source.index("} else {", enable)]
assert "InterlockedExchange64(&a->LastVSyncQpc, 0);" in enable_block
assert "RP_PV_INTSTAT" in enable_block
assert "RP_PV_INTEN" in enable_block
assert "enable | RP_PV_INT_VFP_START" in enable_block

# The ISR, not StartDevice, establishes the hardware phase anchor.
isr = source.index("BOOLEAN NTAPI RpInterrupt")
isr_block = source[isr:]
assert "InterlockedExchange64(&a->LastVSyncQpc, now.QuadPart);" in isr_block
assert "DXGK_INTERRUPT_DISPLAYONLY_VSYNC" in isr_block

# GetScanLine must refuse to invent a phase before the first real edge.
scan = source.index("NTSTATUS APIENTRY RpGetScanLine")
scan_block = source[scan:isr]
assert "if (last == 0)" in scan_block
assert "return STATUS_DEVICE_NOT_READY;" in scan_block
assert "rp_vsync_scanline_from_qpc" in scan_block

print("PASS: StartDevice no longer waits for a disabled VSync source; ISR owns first anchor")
