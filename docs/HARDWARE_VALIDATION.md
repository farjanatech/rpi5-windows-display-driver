# Physical validation gates

Status: **the 0.1.7 firmware-framebuffer path has a recorded working desktop baseline; the 0.1.8 timing/EDID path is not yet hardware-validated.** Hosted CI is a build/test host, not the Pi. Unchecked items below remain required work.

## Baseline record

For each test, retain driver commit, successful CI run URL, configuration, SYS/INF/CAT hashes, signer thumbprint, Windows build/UBR, board revision/RAM, UEFI and submodule commits, firmware settings, HDMI port, monitor and boot mode. Store the local platform report securely; redact private identifiers before posting evidence.

Verify firmware framebuffer reservation/lifetime and its memory/cache layout on the actual platform. Confirm that the ACPI device recognized by Windows is the boot-display owner and that its resources match the reviewed firmware. No hard-coded fallback address is permitted.

## M1: safe access and recovery

- [ ] Read-only collector completes; exact `ACPI\BCM2712` identity is recorded.
- [ ] A restorable image or separate working boot device has been tested.
- [ ] Independent Windows diagnostic/debug access works with the primary display unavailable.
- [ ] Existing display driver/package and all changed lab policies are recorded.
- [ ] Sleep/hibernation remain disabled for this prototype; no power-support claim is made.

## M2/M3: startup and an actual desktop

- [ ] Preflight rejects the wrong device, wrong architecture, wrong commit/signer, expired signer and modified package without side effects.
- [ ] No opt-in prevents startup; an explicit opt-in can be read through the intended device-state API.
- [ ] Installation either selects the driver or reports that Windows retained a higher-ranked driver; package staging is never reported as successful binding.
- [ ] WinDbg identifies the actual loaded `Rpi5Display.sys` and corresponding source/PDB.
- [ ] StartDevice acquires a valid firmware display through the Windows callback and logs dimensions/pitch.
- [ ] Present counters advance while the physical monitor displays correct content.
- [ ] Dirty redraws, overlapping moves, padded strides, colors and Windows software cursor render correctly.
- [ ] Invalid handoff data or allocation failures return errors without guessed mappings.
- [ ] Online rollback and the independently tested recovery route work.

Passing these establishes only a tested prototype for the recorded configuration.

## M3.1: 0.1.8 real timing and EDID

Use UEFI exp0.7 from `farjanatech/rpi5-uefi#9` and driver PR #7 while keeping exp0.6 + 0.1.7 available for recovery.

- [ ] exp0.7 boots with Wi-Fi, fan, NVMe, microSD and USB behavior unchanged from the exp0.6 baseline.
- [ ] Driver 0.1.8 starts with ProblemCode 0 and logs an accepted volatile firmware handoff.
- [ ] The logged firmware display ID matches the firmware-selected output; no 0/1 display-ID assumption is used.
- [ ] Windows Advanced Display shows a numeric refresh rate rather than `Unknown`.
- [ ] `DxgkDdiQueryDeviceDescriptor` exposes the connected monitor's checksum-valid EDID.
- [ ] Reported pixel clock / totals derive to the displayed refresh rate; no hard-coded 60 Hz path is present.
- [ ] Removing/invalidating the handoff falls back to the 0.1.7 unspecified-timing behavior without losing the desktop.
- [ ] Only after the timing checks pass, rerun TestUFO/MotionMark and collect a fresh support bundle.

Passing M3.1 validates metadata/pacing inputs for the recorded monitor/mode; it does not establish hardware acceleration or native HDMI/VSync programming.

## M5: reliability before daily use

Proposed project thresholds, not Microsoft certification:

| Test | Required evidence |
| --- | --- |
| Cold starts | 20 successful cycles on every claimed baseline |
| Restarts | 20 successful cycles |
| Mixed desktop workload | Eight hours without unexplained corruption or a driver crash |
| Install/remove | Repeated cycles, correct active-package reporting and successful fallback |
| Driver Verifier | Target only this driver initially; no unresolved violations, with dump/debug evidence reviewed |
| Lifecycle | Stop/start, ownership release, failed initialization and cleanup without resource leaks/races |
| Diagnostic display | Controlled crash-display test on the sacrificial image, not a production system |
| Claimed power features | Explicit implementation and restoration tests before changing the current unsupported status |

Driver Verifier may deliberately bugcheck a test system. Prepare recovery first; see [Microsoft's guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/driver-verifier). Do not suppress failures, return success from unsupported physical operations, or mark unexecuted tests as passed.

## Later stages

HVS takeover, scanout translation, hardware cursor, real VSync, monitor hot-plug, HDMI mode setting and multiple displays each require a separate resource/register audit, implementation and regression test. Maintain a firmware-framebuffer fallback while bringing up hardware features.

GPU rendering is a separate M7 workstream: controlled off-screen V3D jobs, result verification, memory mapping and context isolation, fences/scheduling, bounded timeout/reset recovery and API-specific user-mode support. A kernel binary, a full-WDDM registration call, or one rendered test image is not evidence of complete Direct3D acceleration.
