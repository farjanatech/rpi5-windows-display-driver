# Raspberry Pi 5 Windows Display Driver

A development project for a Windows-native display driver targeting **Raspberry Pi 5 running Windows 11 ARM64**.

The first goal is reliable HDMI desktop output through a kernel-mode display-only driver. Native display control and hardware-accelerated rendering are later, separately validated milestones.

> **Status: documentation only.** This project currently contains only this README. No driver source, build system, GitHub Actions workflow, signed package, or hardware test results are included. Nothing in this document establishes working Windows driver support or guarantees performance.

## Project scope

| Area | Initial target |
| --- | --- |
| Hardware | Raspberry Pi 5; record the actual board revision and RAM configuration before testing |
| Operating system | Windows 11 ARM64; pin and record the exact test build |
| Display output | One HDMI display, initially retaining the firmware-selected boot mode |
| Driver model | Windows kernel-mode display-only miniport, based on the Microsoft KMDOD sample |
| Planned binary | `Rpi5Display.sys`, with its own service and installation package |
| Firmware baseline | The existing [farjanatech/rpi5-uefi](https://github.com/farjanatech/rpi5-uefi) project, with an exact tested revision recorded |
| Initial rendering scope | Display presentation only; no claim of hardware Direct3D acceleration |
| Current repository contents | `README.md` only |

This is a separate driver project, not a replacement for the UEFI repository. Any necessary firmware changes should be developed and tracked in that repository, with compatible driver and firmware revisions documented together.

## Implementation approach

Use Microsoft's KMDOD sample and the official Windows Driver Kit for the Windows-facing driver interfaces. Audit the sample for ARM64 and Raspberry Pi assumptions rather than treating it as a finished driver.

The planned initial display path is:

```text
Windows display stack
    -> Rpi5Display display-only miniport
    -> validated firmware framebuffer
    -> existing display pipeline
    -> HDMI monitor
```

Obtain adapter resources and boot-display information through documented Windows callbacks. The initial investigation will use `DxgkCbGetDeviceInformation` and `DxgkCbAcquirePostDisplayOwnership`; desktop updates will use `DxgkDdiPresentDisplayOnly`.

Treat successful framebuffer acquisition, valid memory mapping, and presentation on the physical Pi as separate test gates. If valid boot-display information is unavailable, stop and diagnose the firmware/Windows handoff rather than assuming an address or successful display ownership.

The ReactOS fork's Pi-specific `rpi5vc4` work is a reference for hardware investigation and selective, audited reuse. It is not a drop-in Windows driver. Do not make the Windows implementation depend on ReactOS-private presentation interfaces, loader functions, or graphics-kernel internals.

## Development roadmap

Each milestone requires evidence before the next capability is claimed. A successful build does not establish installation, display output, stability, or acceleration.

| Milestone | Work | Required evidence |
| --- | --- | --- |
| **M0 — Project definition** | Establish scope, references, and the implementation sequence. | This README. |
| **M1 — Test platform and firmware contract** | Record the Windows/UEFI/board baseline; verify adapter identity and translated resources; establish diagnostics and rollback. | Platform report, device/resource logs, and a demonstrated recovery procedure. |
| **M2 — Windows ARM64 skeleton** | Adapt KMDOD, pin matching SDK/WDK versions, create an ARM64 build and a uniquely named installation package. | Reproducible build, ARM64 binary inspection, package validation, and import audit. |
| **M3 — First desktop output** | Acquire and validate the boot framebuffer; implement the initial single-mode display contract and presentation path. | The actual driver is loaded, presentation counters advance, and Windows updates appear correctly on the physical monitor. |
| **M4 — Native display features** | Investigate display-engine takeover, hardware cursor, synchronization, and then mode changes. | A separate hardware test and regression report for each enabled feature. |
| **M5 — Stability and recovery** | Implement and test lifecycle, power behavior, cleanup, repeated boot, display transitions, and driver verification. | Repeatable stress results, documented limitations, and successful rollback. |
| **M6 — Packaging and release** | Add reproducible CI, signing/package checks, symbols, hashes, compatibility records, and installation documentation. | A tested package that can be installed, identified as active, and removed. |
| **M7 — Hardware rendering** | Develop V3D bring-up, memory management, submission, recovery, and the necessary user-mode graphics integration. | Correct hardware-rendered application output and fault recovery, with supported APIs explicitly identified. |

All implementation milestones are currently **not started**. M7 is a separate workstream and is not required to prove the first display-only prototype.

## First implementation task

**Create the smallest Windows-native ARM64 display-only driver that can present an updating Windows desktop using the Pi's existing boot framebuffer.**

The first implementation session should:

1. Record the exact Windows build, UEFI revision, board revision, monitor, HDMI port, and boot mode. Verify an independent recovery and diagnostic path.
2. Verify the display adapter's actual Windows hardware IDs and resources. `ACPI\BCM2712` is a candidate from the prior firmware-source review, not proof of the deployed device identity.
3. Pin the Microsoft KMDOD source revision and a matching SDK/WDK toolchain; create an ARM64 solution with a distinct driver name and service.
4. Add initialization and resource logging, then validate the Windows boot-display ownership callback and framebuffer information.
5. Implement the minimum supported display/presentation path and test it on the physical Pi. Keep native mode changes, V3D execution, and hardware-cursor changes out of this first patch.

Do not advertise the first desktop prototype as a stable release. Retain logs showing which driver is active, its initialization result, and its presentation activity.

## Planned development requirements

- A development host with an ARM64-capable Microsoft driver build environment and matching Windows SDK/WDK versions.
- A dedicated Raspberry Pi 5 test installation with known-good firmware and a recoverable Windows image.
- A working HDMI monitor, adequate cooling, and a reliable power supply.
- An independently verified diagnostic connection; firmware UART output alone must not be assumed to provide Windows kernel debugging.
- A documented test-signing and installation procedure appropriate to the test machine's security configuration.

Toolchain versions and commands will be added after a real build is verified. There is no build or install command for this README-only state.

## Safety and implementation rules

- Never overwrite or rename Windows' inbox `BasicDisplay.sys`; use `Rpi5Display.sys` and preserve a recovery route.
- Validate framebuffer addresses, dimensions, pitch, pixel format, mapping bounds, and arithmetic. Review ARM64 cache attributes and coherency explicitly.
- Use Windows-assigned resources and verified device identity rather than hard-coded assumptions from a different firmware or operating system.
- Do not claim unsupported modes, interrupts, power states, WDDM capabilities, or GPU acceleration.
- Audit the KMDOD sample's known limitations, including power-state behavior, before adapting it for a release.
- Coordinate access to shared resources, including firmware-mailbox transactions, before introducing hardware-control paths that could conflict with other drivers.
- Keep firmware, toolchain, operating-system, and driver revisions traceable. Change one major variable at a time during bring-up.
- Test only on a recoverable development system. Do not upload credentials, signing private keys, or unreviewed memory dumps to GitHub.

## Validation and release criteria

The following are proposed project gates, not a claim of Microsoft certification:

| Check | Proposed minimum |
| --- | --- |
| Boot reliability | 20 successful cold starts and 20 restarts on each claimed baseline |
| Desktop workload | Eight hours of mixed activity without display corruption or a driver crash |
| Installation and rollback | Repeated installation/removal with confirmed return to a known-good display driver |
| Monitor and power behavior | Repeatable results for every reconnect, mode, or power transition claimed as supported |
| Driver verification | No unresolved violations in the recorded test configuration |
| Evidence | Source/toolchain/firmware/Windows revisions, package hashes, logs, and known limitations retained |

Record unsupported or untested configurations explicitly. GitHub Actions can validate builds and packaging, but a CI success must not be reported as a Raspberry Pi hardware test.

Performance comparisons must use the same hardware, resolution, firmware, Windows build, and workload. Do not assume that replacing a framebuffer driver automatically improves rendering speed.

## Future source layout

Only `README.md` exists now. After the platform investigation, a possible structure is:

```text
rpi5-windows-display-driver/
    README.md
    driver/                 # Windows ARM64 display-only miniport
    package/                # INF and packaging inputs
    docs/                   # Platform contract, design, build and recovery notes
    tests/                  # Test procedures and non-sensitive results
    scripts/                # Reproducible build and diagnostic helpers
    .github/workflows/      # Build and package checks; not hardware validation
```

These directories, workflows, and code files are planned, not implemented.

## References

### Windows driver foundation

- [Microsoft KMDOD source and README](https://github.com/microsoft/Windows-driver-samples/tree/main/video/KMDOD)
- [KMDOD sample documentation](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/kernel-mode-display-only-miniport-driver-kmdod-sample/)
- [Windows Driver Kit setup](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
- [Display-only driver initialization](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nf-dispmprt-dxgkinitializedisplayonlydriver)
- [Adapter device information](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_get_device_information)
- [Boot-display ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_acquire_post_display_ownership)
- [Display-only presentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_presentdisplayonly)
- [Driver Verifier](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/driver-verifier)

### Platform and hardware investigation

- [Existing Raspberry Pi 5 UEFI project](https://github.com/farjanatech/rpi5-uefi)
- [ReactOS fork: Pi-specific display/GPU work](https://github.com/ahmedarif193/reactos/tree/main-nt10/drivers/directx/rpi5vc4)
- [ReactOS fork: generic software-display component](https://github.com/ahmedarif193/reactos/tree/main-nt10/drivers/directx/softgpu)
- [Linux V3D documentation](https://docs.kernel.org/gpu/v3d.html)
- [Mesa V3D documentation](https://docs.mesa3d.org/drivers/v3d.html)

Reference repositories are research inputs, not project dependencies already imported or evidence that this driver works on Windows.

## Licensing and provenance

No third-party implementation is included in this README-only project. Select and document the project's license before importing implementation code. Track the origin, license, and revision of any subsequently reused material, and retain its applicable notices.

---

Project planning document prepared on **September 26, 2026**. Development and hardware validation are pending.
