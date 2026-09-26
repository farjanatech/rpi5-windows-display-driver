# Raspberry Pi 5 Windows Display Driver

An experimental, Windows-native **ARM64 display-only miniport** for Raspberry Pi 5 running Windows 11. Development now includes driver source, GitHub Actions builds, portable framebuffer tests, and an explicit opt-in lab deployment path.

> **Not hardware validated. Not a production driver.** Building or signing this package does not prove that it starts on a Pi, displays a desktop, survives power transitions, or is safe for daily use. No physical Pi is attached to the hosted CI workflow. Do not install it on your only recoverable Windows system.

## 0.1.1 review and CMD installer

The new [CMD installer guide](docs/CMD_INSTALLER.md) explains `Install.cmd`, `Preflight.cmd`, `Collect-Logs.cmd` and `Uninstall.cmd`, package verification and timestamped local support ZIPs. Use the **Rpi5Display-CMD-Installer-<commit>** artifact from a successful run; extract it fully. Runtime ETW tracing is new in this binary. No driver is installed by CI, and the launchers never silently weaken boot security or reboot the Pi.

Read the [source review and remaining uncertainties](docs/REVIEW_0.1.1.md). A passed software test is not a passed hardware milestone. Existing [issue #2](https://github.com/farjanatech/rpi5-windows-display-driver/issues/2) remains the physical validation gate.

## Current implementation

The first implementation preserves the firmware-selected boot mode and uses documented Windows callbacks to acquire the boot framebuffer. It does not program HDMI, HVS, clocks, mailbox registers, or V3D.

```text
Windows display stack
  -> Rpi5Display.sys (KMDOD, not a render-capable WDDM driver)
  -> checked shadow-buffer moves and dirty-rectangle copies
  -> Windows-supplied firmware framebuffer
  -> existing firmware-configured HDMI output
```

| Area | Implementation / limitation |
| --- | --- |
| Device | Exact `ACPI\BCM2712` hardware-ID match; actual Pi/UEFI identity still must be verified |
| Windows | Native ARM64; Windows 11 client baseline, build 22000 or newer; specific builds remain untested |
| Driver registration | `DxgkInitializeDisplayOnlyDriver`, WDDM 1.2 display-only callback contract |
| Display | One always-connected logical HDMI target; existing boot dimensions; identity transform; 32-bit X8R8G8B8/A8R8G8B8 framebuffer |
| Presentation | Synchronous, checked pitch/bounds, overlap-safe RAM moves, dirty rectangles, shadow restoration, presentation counters |
| Cursor | Windows software cursor; no hardware-cursor claim |
| Safety controls | Per-device `LabEnable` opt-in, no arbitrary-memory IOCTL, no guessed framebuffer address, bounded allocations, drain-before-stop cleanup |
| Power | Visibility and software blanking only; physical suspend/resume and native display power-down are not implemented |
| Acceleration | No V3D jobs, Direct3D hardware acceleration, video decode, render UMD, native mode changes, VSync or multiple outputs |
| Deployment | Lab-only scripts; preflight, explicit confirmed installation, and local log collection; no automatic reboot, boot-security-policy change or firmware flashing |

**Never replace Windows' inbox `BasicDisplay.sys`.** This project builds the separately named `Rpi5Display.sys` and preserves the inbox package for recovery.

## Start here

- [Build and CI instructions](docs/BUILD.md)
- [Lab preflight, installation and rollback](docs/LAB_INSTALL.md)
- [Source/dependency/license audit and reuse decisions](docs/SOURCE_AUDIT.md)
- [Hardware validation checklist](docs/HARDWARE_VALIDATION.md)
- [Original planning roadmap](docs/ROADMAP.md) — retained verbatim as historical context; its documentation-only status and proposed sample-import approach are superseded by this README and the audit.

### Build in GitHub

Push to `main`, open/update a pull request, or manually dispatch **ARM64 driver validation** under [Actions](https://github.com/farjanatech/rpi5-windows-display-driver/actions/workflows/ci.yml). A run validates portable code with AddressSanitizer/UndefinedBehaviorSanitizer and attempts Debug/Release ARM64 builds, PE/INF checks, disposable test signatures, package-integrity tests and installer/diagnostic smoke tests.

Only a run whose required jobs all succeed is a usable *build-validation result*. Download its `Rpi5Display-CMD-Installer-<commit>` artifact for the ready-to-extract Debug installer, or `Rpi5Display-ARM64-LAB-<commit>` for both configurations. Do not install diagnostic artifacts from failed runs. Inspect that exact run's source commit, artifact checksum and signer before using a package. CI success is **not** a Raspberry Pi test result or Microsoft certification.

### First action on the Pi: collect the baseline, not install

Run the reviewed collector from a checked-out source revision, using native ARM64 PowerShell on the Windows Pi:

```powershell
.\scripts\Collect-Platform.ps1 -OutputPath "$env:TEMP\Rpi5Display-platform.json"
```

Alternatively, the extracted CMD artifact includes `Preflight.cmd` and `Collect-Logs.cmd`. Record the firmware commit, board revision, HDMI port and recovery procedure alongside the report. Collection does not install drivers, modify boot/security settings, or upload anything. Review and redact local identifiers before sharing it.

Do not advance to installation until the report, actual device identity, tested recovery route and independent diagnostics have been reviewed. Follow the complete [CMD guide](docs/CMD_INSTALLER.md) and [lab runbook](docs/LAB_INSTALL.md), not an isolated install command.

## Roadmap: implemented code is not a passed milestone

| Gate | Current work / remaining evidence |
| --- | --- |
| **M0 — Definition** | Established; original plan retained in `docs/ROADMAP.md`. |
| **M0A — Audit** | Initial implementation uses original code and documented WDK interfaces. No ReactOS or Microsoft sample source is imported. Later hardware-code imports still need their own transitive dependency/license audit. |
| **M1 — Real platform contract** | Collector and runbook provided; actual Windows/UEFI resources, framebuffer lifetime/cache attributes, recovery and debugger connection must be verified on the Pi. |
| **M2 — ARM64 skeleton/build** | Driver, build scripts, CI, PE/INF validation and test-signing pipeline implemented. Consult the exact Actions run for build results; no blanket green-build claim applies to every commit. |
| **M3 — First desktop** | Firmware-framebuffer presentation code implemented. The physical loaded module, successful POST handoff, advancing counters and correct desktop output are still required. |
| **M4 — Native display features** | Deferred until M1/M3 hardware evidence. Audit/selectively port HVS, cursor, interrupt and mode-setting components one at a time. |
| **M5 — Stability/power** | Stop/cleanup and lab rollback code exist; repeated boot, verifier, stress and supported power-state testing remain pending. No sleep/resume support claim. |
| **M6 — Release** | Lab package pipeline implemented. Production signing, security review, release qualification and compatibility matrix remain pending. |
| **M7 — GPU rendering** | Separate future work: V3D hardware jobs, GPU memory/scheduling/isolation/recovery and API-specific user-mode graphics support. |

The next hardware milestone is **a verified Windows desktop through this driver**, not increasing a WDDM version constant or enabling untested register writes.

## Implementation and licensing decision

The earlier plan proposed adapting Microsoft's KMDOD sample. During implementation, its repository license was verified as **Microsoft Public License (MS-PL)**, which the FSF lists as incompatible with the GPL. This repository already has a GPLv3 license. Therefore, this implementation was written against the documented WDK interfaces **without importing Microsoft sample source**. See [the audit](docs/SOURCE_AUDIT.md) for primary references.

The pinned ReactOS reference remains `ahmedarif193/reactos` commit `b2b6c62a133053c8b3749febace8ce5a8e634936`, `drivers/directx/rpi5vc4`. Its current full-WDDM registration, ReactOS-private presentation interfaces, outside-directory shader dependencies and differing file licenses remain relevant to later selective hardware-code reuse. None of that source is silently linked into this prototype.

New original project source is marked `GPL-3.0-only`; the existing [LICENSE](LICENSE) is retained unchanged. Microsoft SDK/WDK/compiler components have their own terms and are acquired separately for building. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Repository layout

```text
core/                   Checked platform-independent framebuffer operations
driver/                 Windows adapter, mode, presentation, ETW and crash-display callbacks
package/                ARM64 INF input
scripts/                Build, binary/package checks, CMD deployment and diagnostics
tests/                  Portable, package, installer and synthetic trace tests
docs/                   Audit, build, hardware gates and recovery procedures
.github/workflows/      Hosted build validation, not remote Pi installation
```

No signing private keys, credentials, raw memory dumps or device reports belong in this public repository.
