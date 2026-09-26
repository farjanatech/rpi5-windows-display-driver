# Raspberry Pi 5 Windows Display Driver

Experimental Windows-native ARM64 display-only miniport for Raspberry Pi 5 running Windows 11. The project contains driver source, test-signed CMD installer packages, runtime diagnostics and GitHub Actions validation.

> **Current version: 0.1.2. Hardware validation remains pending.** Version 0.1.1 failed initialization on the test Pi because its registration table omitted a required callback. The omission is corrected; a successful new physical startup and desktop have not yet been recorded. Do not reinstall 0.1.1 or equate a green hosted build with a working display.

## 0.1.2 correction

The missing entry was `DxgkDdiDispatchIoRequest`. A Microsoft-signed ARM64 graphics-kernel reference returns `STATUS_REVISION_MISMATCH` for that NULL entry even though the existing interface version is accepted. The new callback rejects unsupported legacy requests without accessing their payloads. The existing KMDOD ABI is retained; this is **not** a GPU-acceleration or WDDM-feature upgrade.

See [root cause, reference identity and regression checks](docs/REGISTRATION_FIX_0.1.2.md). Startup traces now include interface/table/OS details, and support reports include Windows graphics-file versions/hashes. Preflight also checks hibernation before reporting success and handles absent driver properties more clearly.

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
| Display | One logical HDMI target, existing firmware boot mode, 32-bit framebuffer |
| Presentation | Synchronous checked shadow copies, overlapping moves, dirty updates, software cursor |
| Lifecycle | Startup, stop/cleanup, visibility, software blanking, diagnostic-display callbacks |
| Diagnostics | Kernel TraceLogging plus before/after Windows/device/setup/driver records and support ZIPs |
| Safety controls | Explicit per-device LabEnable gate, bounds checks, no guessed physical addresses or arbitrary-memory IOCTL |
| Not implemented | Native HVS/HDMI mode setting, hardware cursor, real VSync, multiple outputs, physical suspend/resume, V3D or Direct3D hardware acceleration |

## Roadmap and evidence

The [original M0-M7 roadmap](docs/ROADMAP.md) is preserved. Its documentation-only starting status and proposed sample import are historical, not the current implementation status.

| Gate | Status |
| --- | --- |
| M0/M0A scope and audit | Initial original-code approach established; future imported hardware code still needs per-file/transitive review |
| M1 platform/recovery contract | User baseline and failed-install/uninstall evidence exist; complete firmware framebuffer lifetime/cache and recovery qualification remains open |
| M2 build/registration | Build/package pipeline exists; 0.1.1 runtime initialization failed; 0.1.2 corrects the identified callback omission and needs a physical retest |
| M3 first desktop | Presentation code exists; correctly updating physical output through this driver is not yet established |
| M4 native display | Separate incremental hardware work, gated on the firmware-framebuffer baseline |
| M5 reliability/power | Verifier, repeated boot, stress, lifecycle and supported power transitions require actual tests |
| M6 release | Lab packaging exists; production signing and release qualification remain pending |
| M7 rendering | Separate future V3D memory/submission/scheduling/recovery and API-specific user-mode work |

[Issue #2](https://github.com/farjanatech/rpi5-windows-display-driver/issues/2) remains the physical-test gate. See the [hardware checklist](docs/HARDWARE_VALIDATION.md), [0.1.1 review](docs/REVIEW_0.1.1.md) and [historical first-build record](docs/VALIDATION_RECORD.md). Historical artifact pins never apply to later builds.

## Provenance

The driver is original code using official WDK interfaces. No Microsoft MS-PL sample implementation or ReactOS implementation is bundled. Microsoft SDK/WDK/compiler components retain their separate terms. The initial sample-import plan was changed after its license was audited; see [SOURCE_AUDIT.md](docs/SOURCE_AUDIT.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The later Pi hardware reference remains `ahmedarif193/reactos` commit `b2b6c62a133053c8b3749febace8ce5a8e634936`, `drivers/directx/rpi5vc4`. Its private ReactOS interfaces and external shader dependencies are not required by this prototype. New original code is `GPL-3.0-only`; the existing [LICENSE](LICENSE) remains unchanged.

Keep signing private keys, credentials, raw dumps and private device reports out of this public repository. Diagnostic ZIPs stay local and must be reviewed for private identifiers before sharing.
