# Raspberry Pi 5 Windows Display Driver

Experimental Windows-native ARM64 display-only miniport for Raspberry Pi 5 running Windows 11. The project contains driver source, test-signed CMD installer packages, runtime diagnostics and GitHub Actions validation.

> **Current development candidate: 0.1.8. Known-good hardware baseline: 0.1.7.** The 0.1.7 framebuffer/display-only path has reached a working Windows desktop on the Raspberry Pi 5. Version 0.1.8 keeps that present path and adds validated firmware timing/EDID metadata; its physical timing/EDID result is the next hardware gate. Do not equate a green hosted build with a passed Pi test.

## 0.1.8 timing/EDID handoff

The driver consumes a versioned volatile UEFI runtime handoff from the paired exp0.7 firmware candidate. It validates the firmware variable attributes, exact POST geometry, progressive timing, pixel clock/totals and complete EDID chain. Valid data is returned through `DxgkDdiQueryDeviceDescriptor` and used for VidPN signal timing; missing/invalid data falls back to the hardware-working 0.1.7 unspecified-timing behavior.

No 60 Hz value is hard-coded. No HVS/V3D/HDMI register programming, hardware acceleration, new mode or interrupt-driven VSync path is added. The paired firmware work is tracked in `farjanatech/rpi5-uefi#9`; this driver candidate is PR #7.

## Build and use

[GitHub Actions](https://github.com/farjanatech/rpi5-windows-display-driver/actions/workflows/ci.yml) runs portable sanitizer tests and Debug/Release ARM64 builds, binary/INF/catalog checks, test-signing, package authentication, CMD checks and diagnostic-bundle tests. Use an artifact from a run whose required jobs all succeeded.

Download the **`Rpi5Display-CMD-Installer-<commit>`** artifact and extract the entire ZIP into a new local folder on the Windows Pi. Verify its digest against the trusted run. The package includes `Install.cmd`, `Preflight.cmd`, `Collect-Logs.cmd`, `Uninstall.cmd`, support scripts, symbols, source and `READ-ME-FIRST.md`.

`Install.cmd` performs checks, requests UAC and explicit `INSTALL` confirmation, captures startup/runtime diagnostics and writes a support ZIP. No WDK, Git, Python or separate PowerShell 7 is needed on the Pi. No firmware flashing, forced driver ranking, automatic reboot or automatic log upload is performed. The lab signing certificate is imported only by an explicitly confirmed installation.

**Use a restorable test image and independent recovery/debug access.** Keep the known-good Windows display driver intact; never overwrite inbox `BasicDisplay.sys`. This project installs the separately named `Rpi5Display.sys`. Physical suspend/resume is unsupported, so the controlled test requires hibernation and automatic sleep disabled. Existing preparation need not be repeated for a new package version.

Full instructions: [CMD installer and log collection](docs/CMD_INSTALLER.md), [lab prerequisites and rollback](docs/LAB_INSTALL.md), [build tools and checks](docs/BUILD.md), [temporary build-host signing trust](docs/BUILD_HOST_TRUST.md).

## Implemented scope

| Area | Current implementation / limitation |
| --- | --- |
| Device | Exact `ACPI\BCM2712` match; actual firmware resource ownership must be verified |
| Display | One logical firmware-selected target, existing boot mode, 32-bit framebuffer; validated timing/EDID from exp0.7 when available |
| Presentation | Synchronous checked shadow copies, overlapping moves, dirty updates, software cursor |
| Lifecycle | Startup, stop/cleanup, visibility, software blanking, diagnostic-display callbacks |
| Diagnostics | Kernel TraceLogging plus before/after Windows/device/setup/driver records and support ZIPs |
| Safety controls | Explicit per-device LabEnable gate, bounds checks, no guessed physical addresses or arbitrary-memory IOCTL |
| Not implemented | Native HVS/HDMI mode setting, hardware cursor, interrupt-driven VSync synchronization, multiple outputs, physical suspend/resume, V3D or Direct3D hardware acceleration |

## Roadmap and evidence

The [original M0-M7 roadmap](docs/ROADMAP.md) is preserved. Its documentation-only starting status and proposed sample import are historical, not the current implementation status.

| Gate | Status |
| --- | --- |
| M0/M0A scope and audit | Initial original-code approach established; future imported hardware code still needs per-file/transitive review |
| M1 platform/recovery contract | User baseline and failed-install/uninstall evidence exist; complete firmware framebuffer lifetime/cache and recovery qualification remains open |
| M2 build/registration | Build/package pipeline exists; registration/lifecycle fixes through 0.1.7 have been exercised on the physical Pi |
| M3 first desktop | 0.1.7 has produced a working firmware-framebuffer Windows desktop; 0.1.8 timing/EDID reporting is the current hardware gate |
| M4 native display | Separate incremental hardware work, gated on the firmware-framebuffer baseline |
| M5 reliability/power | Verifier, repeated boot, stress, lifecycle and supported power transitions require actual tests |
| M6 release | Lab packaging exists; production signing and release qualification remain pending |
| M7 rendering | Separate future V3D memory/submission/scheduling/recovery and API-specific user-mode work |

[Issue #2](https://github.com/farjanatech/rpi5-windows-display-driver/issues/2) remains the physical-test gate. See the [hardware checklist](docs/HARDWARE_VALIDATION.md), [0.1.1 review](docs/REVIEW_0.1.1.md) and [historical first-build record](docs/VALIDATION_RECORD.md). Historical artifact pins never apply to later builds.

## Provenance

The driver is original code using official WDK interfaces. No Microsoft MS-PL sample implementation or ReactOS implementation is bundled. Microsoft SDK/WDK/compiler components retain their separate terms. The initial sample-import plan was changed after its license was audited; see [SOURCE_AUDIT.md](docs/SOURCE_AUDIT.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The later Pi hardware reference remains `ahmedarif193/reactos` commit `b2b6c62a133053c8b3749febace8ce5a8e634936`, `drivers/directx/rpi5vc4`. Its private ReactOS interfaces and external shader dependencies are not required by this prototype. New original code is `GPL-3.0-only`; the existing [LICENSE](LICENSE) remains unchanged.

Keep signing private keys, credentials, raw dumps and private device reports out of this public repository. Diagnostic ZIPs stay local and must be reviewed for private identifiers before sharing.
