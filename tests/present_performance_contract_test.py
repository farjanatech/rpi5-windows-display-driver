#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Static regression for the 0.1.18 Present-phase diagnostic hot paths."""

from pathlib import Path

present = Path("driver/present.c").read_text(encoding="utf-8")
vsync = Path("driver/vsync.c").read_text(encoding="utf-8")
adapter = Path("driver/adapter.c").read_text(encoding="utf-8")
framebuffer = Path("core/framebuffer.h").read_text(encoding="utf-8")

# The write-combined framebuffer is memory, not a register bank. A full 1080p
# update must not execute a register-buffer operation (and its ordering
# semantics) once per scan line.
flush_start = present.index("VOID RpFlush")
blank_start = present.index("VOID RpBlank")
flush = present[flush_start:blank_start]
assert "WRITE_REGISTER_BUFFER_ULONG(" not in flush
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

# Shadow-buffer copies/moves must use bulk memory primitives. The exhaustive
# framebuffer test separately validates all overlap directions under ASan/UBSan.
assert "RP_MEMCPY(dst->data + d, src->data + s, count);" in framebuffer
assert "RP_MEMMOVE(s->data + d, s->data + a, count);" in framebuffer
assert "for (x = 0; x < count;" not in framebuffer
assert "for (x = count; x != 0;" not in framebuffer

# Keep high-resolution timing and copy-area evidence for idle degradation.
assert "PresentMaxUs" in present
assert "KeQueryPerformanceCounter" in present
assert "KeQueryInterruptTime" not in present
assert "PresentTotalPixels" in present
assert "PresentMaxPixels" in present
assert "PresentOver16ms" in present
assert "pixels=%llu" in present
assert "maxPresentUs=" in adapter

# QPC frequency must be initialized unconditionally in StartDevice, not only by
# the optional hardware-VSync initializer.
start = adapter[adapter.index("NTSTATUS NTAPI RpStart"):adapter.index("NTSTATUS NTAPI RpStop")]
assert "KeQueryPerformanceCounter(&qpcFrequency)" in start
assert "a->QpcFrequency = qpcFrequency.QuadPart;" in start

# The phase tracer must be TraceLogging-only: do not call RP_LOG/DbgPrint on
# every Present. Entry/lock/shadow/flush phases make a blocked call observable.
trace = Path("driver/trace.c").read_text(encoding="utf-8")
assert "VOID RpTracePresentPhase" in trace
phase = trace[trace.index("VOID RpTracePresentPhase"):]
assert 'TraceLoggingWrite(' in phase
assert 'DbgPrintEx' not in phase
assert 'RP_LOG(' not in phase
assert "RpPresentPhaseEntry" in present
assert "RpPresentPhaseLocked" in present
assert "RpPresentPhaseShadowDone" in present
assert "RpPresentPhaseFlushDone" in present
assert "PresentEntered" in present
assert "PresentCompleted" in present

# Successful Windows VSync-control chatter must not write dozens of registry
# values on every enable/disable transition. Preserve first/failure evidence
# plus a periodic snapshot instead.
assert "#define RP_VSYNC_CONTROL_PERSIST_MASK   63ULL" in vsync
assert "static BOOLEAN RpShouldPersistControlResult" in vsync
record = vsync[
    vsync.index("static VOID RpRecordControlInterruptResult"):
    vsync.index("BOOLEAN RpVSyncRegistrationAvailable")
]
gate = record.index("RpShouldPersistControlResult")
counters = record.index("RpRecordVSyncCounters(a);")
assert gate < counters

# Do not "optimize" away the DPC path: the Microsoft KMDOD interrupt pattern
# still notifies dxgkrnl and queues a DPC.
isr = vsync[vsync.index("BOOLEAN NTAPI RpInterrupt"):]
assert "DxgkCbNotifyInterrupt" in isr
assert "DxgkCbQueueDpc" in isr
assert "DxgkCbNotifyDpc" in adapter

print("PASS: 0.1.18 has independent QPC and trace-only Present phase diagnostics")
