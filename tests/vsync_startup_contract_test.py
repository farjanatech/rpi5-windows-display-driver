#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Static regression for the 0.1.17 simulated-VSync A/B KMDOD contract."""

from pathlib import Path

adapter = Path("driver/adapter.c").read_text(encoding="utf-8")
vsync = Path("driver/vsync.c").read_text(encoding="utf-8")
modes = Path("driver/modes.c").read_text(encoding="utf-8")
header = Path("driver/display.h").read_text(encoding="utf-8")
inf = Path("package/Rpi5Display.inf").read_text(encoding="utf-8")
registration = Path("core/registration_contract.h").read_text(encoding="utf-8")

assert 'RP_DRIVER_VERSION "0.1.17-ab-simulated-vsync"' in header
assert "#define RP_WINDOWS_VSYNC_MODE_SIMULATED 1u" in header
assert "DriverVer=09/29/2026,0.1.17.0" in inf

# Hardware VSync code stays compiled for the controlled A/B experiment.
assert "NTSTATUS RpVSyncInitialize" in vsync
assert "NTSTATUS APIENTRY RpControlInterrupt" in vsync
assert "NTSTATUS APIENTRY RpGetScanLine" in vsync
assert "BOOLEAN NTAPI RpInterrupt" in vsync
assert "DXGK_INTERRUPT_DISPLAYONLY_VSYNC" in vsync

# This build must not bind the optional VSync callback pair.
driver_entry = adapter[
    adapter.index("NTSTATUS NTAPI DriverEntry"):
    adapter.index("NTSTATUS NTAPI RpAdd")
]
assert "gRpVSyncHardwareAvailable = RpVSyncRegistrationAvailable();" in driver_entry
assert "gRpVSyncRegistrationEnabled = FALSE;" in driver_entry
assert "RP_BIND_VSYNC_CALLBACK" not in driver_entry
assert "RP_DOD_VSYNC_CALLBACK_BINDINGS(RP_BIND_VSYNC_CALLBACK)" not in driver_entry

# InterruptRoutine/DpcRoutine remain in the base KMDOD callback table, matching
# Microsoft's KMDOD pattern when ControlInterrupt/GetScanLine are omitted.
assert "X(DxgkDdiInterruptRoutine, RpInterrupt)" in registration
assert "X(DxgkDdiDpcRoutine, RpDpc)" in registration

# StartDevice must only initialize PixelValve VSync when it was advertised.
start = adapter[
    adapter.index("NTSTATUS NTAPI RpStart"):
    adapter.index("NTSTATUS NTAPI RpStop")
]
assert "if (a->VSyncAdvertised)" in start
assert "RpVSyncInitialize" in start
assert 'L"Rpi5DisplayWindowsVSyncMode"' in adapter
assert 'L"Rpi5DisplayVSyncHardwareAvailable"' in adapter

# Windows requires unspecified signal frequency fields when KMDOD VSync control
# is not supported. The real-timing path is gated by VSyncAdvertised.
signal = modes[
    modes.index("static VOID RpSignal"):
    modes.index("static BOOLEAN RpSourceValid")
]
assert "a->VSyncAdvertised && a->FirmwareTimingValid && a->VSyncHardwareReady" in signal
assert "s->PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;" in signal
assert "s->VSyncFreq.Numerator = s->VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;" in signal
assert "s->HSyncFreq.Numerator = s->HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;" in signal

# Preserve the 0.1.16 one-shot anchor cleanup in the dormant hardware path.
anchor_reset = "InterlockedExchange(&a->VSyncAnchorReported, 0);"
init = vsync.index("NTSTATUS RpVSyncInitialize")
control_struct = vsync.index("typedef struct RP_VSYNC_CONTROL_CONTEXT")
init_block = vsync[init:control_struct]
setter = vsync.index("static BOOLEAN RpSetVSyncSynchronized")
shutdown = vsync.index("VOID RpVSyncShutdown")
setter_block = vsync[setter:shutdown]
isr = vsync[vsync.index("BOOLEAN NTAPI RpInterrupt"):]
assert init_block.count(anchor_reset) == 1
assert anchor_reset not in setter_block
assert anchor_reset not in isr

print("PASS: 0.1.17 omits KMDOD VSync control and forces Windows simulated timing")
