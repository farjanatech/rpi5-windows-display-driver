#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Static regression for the 0.1.15 KMDOD VSync-control contract."""

from pathlib import Path

source = Path("driver/vsync.c").read_text(encoding="utf-8")
header = Path("driver/display.h").read_text(encoding="utf-8")
inf = Path("package/Rpi5Display.inf").read_text(encoding="utf-8")

assert 'RP_DRIVER_VERSION "0.1.15-hzfix-idle-power"' in header
assert "DriverVer=09/28/2026,0.1.15.0" in inf

assert "RP_VSYNC_PHASE_PROVISIONAL" in source
assert "RP_VSYNC_PHASE_HARDWARE" in source
assert "seed = KeQueryPerformanceCounter(&frequency);" in source
assert "VSyncProvisionalScanLineQueries" in source

# A provisional timing phase must exist before the first hardware IRQ.
init = source.index("NTSTATUS RpVSyncInitialize")
control_struct = source.index("typedef struct RP_VSYNC_CONTROL_CONTEXT")
init_block = source[init:control_struct]
assert "RP_VSYNC_PHASE_PROVISIONAL" in init_block
assert 'L"Rpi5DisplayVSyncProvisionalPhaseReady", 1' in init_block

# Enabling must publish the software-enabled state before PV_INTEN is unmasked,
# eliminating the 0.1.11 first-edge race.
setter = source.index("static BOOLEAN RpSetVSyncSynchronized")
shutdown = source.index("VOID RpVSyncShutdown")
setter_block = source[setter:shutdown]
enable = setter_block.index("if (control->Enable)")
enable_block = setter_block[enable:setter_block.index("} else {", enable)]
flag_pos = enable_block.index("InterlockedExchange(&a->VSyncInterruptEnabled, 1);")
inten_pos = enable_block.index("enable | RP_PV_INT_VFP_START")
assert flag_pos < inten_pos

# Windows 11's KMDOD path was observed requesting DISPLAYONLY_VSYNC (5) in
# ControlInterrupt. Accept both the generic CRTC value and the KMDOD value.
control = source.index("NTSTATUS APIENTRY RpControlInterrupt")
scan = source.index("NTSTATUS APIENTRY RpGetScanLine")
control_block = source[control:scan]
assert "interruptType != DXGK_INTERRUPT_CRTC_VSYNC &&" in control_block
assert "interruptType != DXGK_INTERRUPT_DISPLAYONLY_VSYNC" in control_block
assert "ControlInterrupt VSYNC type=%u" in control_block

# STATUS_UNSUCCESSFUL from DxgkCbSynchronizeExecution gets the documented
# interrupt-not-connected-yet fallback, while the result is persisted.
assert "synchronizeStatus == STATUS_UNSUCCESSFUL" in control_block
assert "usedDirectFallback = TRUE;" in control_block
assert "applied = RpSetVSyncSynchronized(&control);" in control_block
assert "RpRecordControlInterruptResult" in control_block
assert "Rpi5DisplayVSyncLastControlPvInten" in source
assert "Rpi5DisplayVSyncLastEnableType" in source
assert "Rpi5DisplayVSyncLastEnablePvInten" in source
assert "Rpi5DisplayVSyncLastDisableType" in source

# GetScanLine accepts provisional phase, but only a real ISR upgrades it to
# a hardware anchor reported to dxgkrnl.
scan_block = source[scan:source.index("BOOLEAN NTAPI RpInterrupt")]
assert "RP_VSYNC_PHASE_PROVISIONAL" in scan_block
assert "Rpi5DisplayVSyncProvisionalPhaseUsed" in scan_block
isr = source[source.index("BOOLEAN NTAPI RpInterrupt"):]
assert "RP_VSYNC_PHASE_HARDWARE" in isr
count_pos = isr.index("InterlockedIncrement64(&a->VSyncCount);")
notify_pos = isr.index("DxgkCbNotifyInterrupt")
assert count_pos < notify_pos

# StopDevice must retain anchor/counter evidence rather than overwrite it.
shutdown_block = source[shutdown:control]
assert 'L"Rpi5DisplayVSyncAnchorReady", 0' not in shutdown_block
assert "Rpi5DisplayVSyncInterruptEnabledBeforeStop" in shutdown_block

print("PASS: 0.1.15 preserves the working KMDOD DISPLAYONLY_VSYNC contract")
