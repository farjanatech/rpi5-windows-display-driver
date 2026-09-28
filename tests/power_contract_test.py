#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Static regression for the 0.1.15 idle/power-state contract."""

from pathlib import Path

adapter = Path("driver/adapter.c").read_text(encoding="utf-8")
header = Path("driver/display.h").read_text(encoding="utf-8")
inf = Path("package/Rpi5Display.inf").read_text(encoding="utf-8")
collector = Path("scripts/Collect-Platform.ps1").read_text(encoding="utf-8")

assert 'RP_DRIVER_VERSION "0.1.15-hzfix-idle-power"' in header
assert "DriverVer=09/28/2026,0.1.15.0" in inf

start = adapter.index("NTSTATUS NTAPI RpPower")
end = adapter.index("VOID NTAPI RpReset", start)
power = adapter[start:end]

# Microsoft documents SetPowerState as a transition that should not reject a
# valid D-state request. The display-only sample also returns success for
# adapter D-state changes even without physical power gating.
assert "STATUS_NOT_SUPPORTED" not in power
assert "return STATUS_SUCCESS;" in power
assert "PAGED_CODE();" in power

# Idle power transitions must not synchronously push or blank the full
# framebuffer. Restore with the next present instead.
assert "RpFlush(" not in power
assert "RpBlank(" not in power
assert "a->NeedFull = TRUE;" in power
assert "previous != PowerDeviceD0" in power

# Preserve post-mortem evidence and collect the display-idle policy.
assert "RpRecordPowerState" in adapter
assert "PowerRequests" in header
assert "Rpi5DisplayLastPowerState" in collector
assert "SUB_VIDEO" in collector

print("PASS: 0.1.15 accepts idle D-state transitions and defers full framebuffer repair")
