# Raspberry Pi 5 Windows Display Driver

Experimental Windows-native ARM64 display-only miniport for Raspberry Pi 5 running Windows 11. The project contains driver source, test-signed CMD installer packages, runtime diagnostics and GitHub Actions validation.

> **Branch candidate: 0.1.9-hzfix on `sub-1st-hzfix`. Known-good desktop baseline: 0.1.7; 0.1.8 has also been reported by the tester to install and boot cleanly with UEFI exp0.7 but still shows an unknown refresh rate.** This branch preserves the existing framebuffer/PresentDisplayOnly path and adds the Windows-required hardware VSync control contract. It is not hardware-validated yet. Do not equate a green hosted build with a passed Pi test.

## 0.1.9-hzfix hardware VSync experiment

The driver still consumes the versioned volatile `Rpi5DisplayHandoff` produced by UEFI exp0.7 and validates its exact POST geometry, timing and EDID. The Hz-fix branch additionally implements the KMDOD VSync-control contract required before reporting real `PixelRate`, `HSyncFreq` and `VSyncFreq`: `DxgkDdiControlInterrupt`, `DxgkDdiGetScanLine`, the existing ISR and DPC are supplied together only when a valid exp0.7 handoff is already available.

For HDMI0/HDMI1 the branch maps only the exact PixelValve MMIO resource assigned by Windows, verifies active scanout, uses the real PixelValve VFP-start interrupt as the VSync anchor, reports `DXGK_INTERRUPT_DISPLAYONLY_VSYNC` to dxgkrnl, and derives scan-line phase from that hardware anchor plus the validated firmware timing. It does not program display modes, HVS lists, HDMI, or V3D. If the exp0.7 handoff is absent at registration, the optional VSync pair is left unbound and the established unspecified-timing fallback is retained.

No refresh rate is hard-coded. This branch still provides no V3D/Direct3D rendering acceleration and is not expected to create a Task Manager GPU-engine graph. Physical Pi validation is required before merging it to `main`.

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
| Display | One logical firmware-selected target, existing boot mode, 32-bit framebuffer; validated timing/EDID from exp0.7; experimental PixelValve-backed VSync on `sub-1st-hzfix` |
| Presentation | Synchronous checked shadow copies, overlapping moves, dirty updates, software cursor |
| Lifecycle | Startup, stop/cleanup, visibility, software blanking, diagnostic-display callbacks |
| Diagnostics | Kernel TraceLogging plus before/after Windows/device/setup/driver records and support ZIPs |
| Safety controls | Explicit per-device LabEnable gate, bounds checks, no guessed physical addresses or arbitrary-memory IOCTL |
| Not implemented | Native HVS/HDMI mode setting, hardware cursor, multiple outputs, physical suspend/resume, V3D or Direct3D hardware acceleration; the branch VSync path observes existing firmware scanout only |

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
