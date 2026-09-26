# Raspberry Pi 5 Windows Display Driver

A development project for a Windows-native display driver targeting **Raspberry Pi 5 running Windows 11 ARM64**.

The first goal is reliable HDMI desktop output through a kernel-mode display-only driver. Native display control and hardware-accelerated rendering are later, separately validated milestones.

**Implementation strategy: use the official Windows WDK and Microsoft's KMDOD foundation for Windows integration, and selectively adapt audited Raspberry Pi hardware work from Ahmed Arif's ReactOS `rpi5vc4` implementation. Do not unnecessarily rewrite reusable hardware logic, and do not import the ReactOS graphics stack unchanged.**

> **Status: documentation only.** This repository currently contains `README.md` and `LICENSE`. No driver source, build system, GitHub Actions workflow, signed package, or hardware test results are included. The source findings below are not a successful Windows build or a hardware test. Nothing here establishes working Windows driver support or guarantees performance.

## Project scope

| Area | Initial target |
| --- | --- |
| Hardware | Raspberry Pi 5; record the actual board revision and RAM configuration before testing |
| Operating system | Windows 11 ARM64; pin and record the exact test build |
| Display output | One HDMI display, initially retaining the firmware-selected boot mode |
| Driver model | Windows kernel-mode display-only miniport, based on the Microsoft KMDOD sample |
| Planned binary | `Rpi5Display.sys`, with its own service and installation package |
| Firmware baseline | The existing [farjanatech/rpi5-uefi](https://github.com/farjanatech/rpi5-uefi) project, with an exact tested revision recorded |
| Pi-specific reference | The pinned ReactOS `rpi5vc4` implementation below; selective reuse requires a dependency, Windows-compatibility, and license audit |
| Initial rendering scope | Display presentation only; no claim of hardware Direct3D acceleration |
| Current repository contents | `README.md` and `LICENSE` only |

This is a separate driver project, not a replacement for the UEFI repository. Any necessary firmware changes should be developed and tracked in that repository, with compatible driver and firmware revisions documented together.

## Source-review baseline and new findings

The September 26, 2026 source review used this immutable reference:

| Item | Pinned reference |
| --- | --- |
| Repository | `ahmedarif193/reactos` |
| Branch reviewed | `main-nt10` |
| Commit | `b2b6c62a133053c8b3749febace8ce5a8e634936` |
| Component | [`drivers/directx/rpi5vc4`](https://github.com/ahmedarif193/reactos/tree/b2b6c62a133053c8b3749febace8ce5a8e634936/drivers/directx/rpi5vc4) |
| Evidence level | Source inspection only; not compiled or tested on Windows for this project |

This is a review baseline, not a claim that the branch will remain at this revision. Record subsequent upstream changes explicitly rather than silently following a moving branch.

### What the code actually establishes

| Finding at the pinned revision | Consequence for this project |
| --- | --- |
| The actual `DriverEntry` uses `DRIVER_INITIALIZATION_DATA`, selects `DXGKDDI_INTERFACE_VERSION_WDDM2_0`, registers rendering/memory/scheduling callbacks, and calls `DxgkInitialize`. Some header/build comments still describe a display-only driver. | Treat the executable registration path as authoritative. The existing component is not merely the KMDOD described by those comments. A declared interface version does not establish complete WDDM behavior or Windows compatibility. |
| The main driver exposes a private ReactOS shadow-present interface; its header includes ReactOS presentation, loader-framebuffer, and graphics-interface headers. | Separate the Windows integration layer from reusable hardware logic. Do not carry the private desktop-presentation contract into the Windows driver. |
| The source contains HVS/cursor handling, firmware-framebuffer paths, ACPI output/EDID handling, and V3D-related modules. | Audit these as reuse candidates instead of starting all hardware work from zero. Presence of code is not proof of correctness or hardware support. |
| The build references shader material under `win32ss/drivers/miniport/rpi5vc4`, outside the linked directory, and uses ReactOS build helpers/libraries. | Inventory transitive dependencies before selecting files. Copying only this directory is not a standalone Windows build plan. |
| The power-state implementation explicitly lacks PixelValve/PHY power-down and uses framebuffer blanking as an approximation of off. | Keep real power management and resume validation as explicit work. A successful callback return is not evidence that the hardware entered the requested state. |
| The main driver file is marked `GPL-2.0-or-later`, while `rpi5vc4_scanout.c` is marked `GPL-3.0-or-later`. | Audit licenses per file, including dependencies and shader material; do not assume one uniform upstream license. Preserve notices and resolve compatibility before import. |

Pinned evidence: [driver implementation and registration](https://github.com/ahmedarif193/reactos/blob/b2b6c62a133053c8b3749febace8ce5a8e634936/drivers/directx/rpi5vc4/rpi5vc4.c), [shared header](https://github.com/ahmedarif193/reactos/blob/b2b6c62a133053c8b3749febace8ce5a8e634936/drivers/directx/rpi5vc4/rpi5vc4.h), [build configuration](https://github.com/ahmedarif193/reactos/blob/b2b6c62a133053c8b3749febace8ce5a8e634936/drivers/directx/rpi5vc4/CMakeLists.txt), and [ACPI/scanout source](https://github.com/ahmedarif193/reactos/blob/b2b6c62a133053c8b3749febace8ce5a8e634936/drivers/directx/rpi5vc4/rpi5vc4_scanout.c).

## Implementation approach

### Windows-native foundation first

Use Microsoft's KMDOD sample and the official Windows Driver Kit for the Windows-facing driver interfaces. Audit the sample for ARM64 and Raspberry Pi assumptions rather than treating it as a finished driver. Microsoft documents its UEFI-framebuffer use and its sample-specific sleep limitation in the [KMDOD documentation](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/kernel-mode-display-only-miniport-driver-kmdod-sample/).

The planned initial display path is:

```text
Windows display stack
    -> Rpi5Display display-only miniport
    -> validated firmware framebuffer
    -> existing display pipeline
    -> HDMI monitor
```

For this project's initial KMDOD, use the WDK's `KMDDOD_INITIALIZATION_DATA`, `DxgkInitializeDisplayOnlyDriver`, and `Displib.lib` as documented by [Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nf-dispmprt-dxgkinitializedisplayonlydriver). This is a deliberate architecture choice; it is not a description of the pinned ReactOS driver's current full-WDDM registration.

Obtain adapter resources and boot-display information through documented Windows callbacks: `DxgkCbGetDeviceInformation` and `DxgkCbAcquirePostDisplayOwnership`. Desktop updates will use `DxgkDdiPresentDisplayOnly`. Start with synchronous presentation, correct pitch/bounds handling, and screen moves completed before dirty-rectangle copies, following the [presentation contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_presentdisplayonly).

Treat successful framebuffer acquisition, valid memory mapping, and presentation on the physical Pi as separate test gates. If valid boot-display information is unavailable, stop and diagnose the firmware/Windows handoff rather than assuming an address or successful display ownership.

### Selective reuse, not an unchanged driver transplant

Keep the Windows callback layer, platform-resource handling, display hardware backend, and eventual rendering backend separate. A hardware module is eligible for reuse only after its dependencies, memory accesses, synchronization, compiler assumptions, and provenance are understood.

Do not copy ReactOS's `DriverEntry` into the KMDOD and simply rename its initialization routine. The initialization structures and callback contracts must match the chosen Windows driver model. Do not import a ReactOS graphics-kernel resolver, hand-recreate WDK structure layouts, or increase advertised WDDM capabilities merely to make a build compile.

### Component reuse decisions

These are planned dispositions, not completed audits or approved imports.

| Source area | Planned treatment | Gate |
| --- | --- | --- |
| `rpi5vc4.c` / `rpi5vc4.h` | Separate useful hardware/lifecycle ideas from ReactOS-specific platform detection, loader access, private presentation, and registration. Implement the Windows-facing layer with official WDK contracts. | M0A dependency/callback audit; M2 Windows skeleton |
| `rpi5vc4_scanout.c`, `rpi5vc4_present.c`, `rpi5vc4_vidpn.c` | Evaluate ACPI output discovery, framebuffer geometry, presentation, and mode handling individually. Replace private desktop interfaces; expose only the modes and outputs actually supported. | M0A per-file review; M3 tested firmware-framebuffer path |
| `rpi5vc4_hvs.c`, `rpi5vc4_crtc.c`, related headers and cursor logic | Candidate native display backend. Audit register access, scanout addresses, HVS translation, synchronization, and ownership before enabling hardware writes. | M4 incremental hardware tests |
| `rpi5vc4_mbox.c` and related helpers | Candidate firmware communication reference; define resource ownership, bounded transactions, and serialization with other mailbox users first. | M1 resource contract; M4 integration review |
| `rpi5vc4_wddm.c`, `rpi5vc4_v3d.c`, `rpi5vc4_v3d_exec.c` | Retain as later rendering references. Do not compile the full rendering/scheduling path into the first display-only prototype merely to satisfy dependencies. | M7 separate rendering architecture and validation |
| Shader material, shared headers, libraries, and registry/build helpers outside this directory | Inventory origin and exact revision; decide whether to replace, defer, or import with retained notices. Do not pull in the entire ReactOS tree by accident. | M0A transitive dependency and provenance review |

## Development roadmap

Each milestone requires evidence before the next capability is claimed. A successful build does not establish installation, display output, stability, or acceleration.

**M0A is the new prerequisite for implementation-code reuse.** Existing M1-M7 identifiers are retained so the earlier roadmap remains traceable. The limited source findings above do not mean the full M0A audit is finished.

| Milestone | Work | Required evidence |
| --- | --- | --- |
| **M0 — Project definition** | Establish scope, pinned review baseline, architecture, references, and the implementation sequence. | This updated README; documentation only. |
| **M0A — Source, dependency, and license audit** | Inventory the pinned `rpi5vc4` files and transitive dependencies; classify reuse/rewrite/defer; compare actual callbacks with WDK contracts; record per-file licenses and origins. | A reviewable import plan with source commits, dependency map, Windows blockers, and an explicit decision for each proposed import. No unresolved provenance or required dependency for imported files. |
| **M1 — Test platform and firmware contract** | Record the Windows/UEFI/board baseline; inspect actual adapter identity and assigned resources; establish independent diagnostics, a restorable image, and rollback. Document framebuffer lifetime, resource ownership, and firmware requirements. | Platform report, live device/resource inventory, pinned firmware/submodule revisions, and a demonstrated recovery procedure. Driver-callback validation follows in M2/M3. |
| **M2 — Windows ARM64 skeleton and build checks** | Adapt the audited KMDOD foundation; pin matching SDK/WDK and sample revisions; build a uniquely named ARM64 package with startup/resource logging. Add reproducible CI build/package checks once the local build works. | ARM64 binary/import inspection, package validation, reproducible build logs, and documented initialization/ownership results on the test Pi. No required ReactOS-private runtime interfaces. CI is not a hardware pass. |
| **M3 — First desktop output** | Acquire and validate the boot framebuffer; implement the single-output, single-mode display contract and synchronous presentation. Keep native mode changes and V3D execution disabled. | The actual driver is loaded, presentation counters advance, and Windows updates appear correctly on the physical monitor. Invalid handoff/mapping paths fail safely and rollback works. |
| **M4 — Native display features** | Selectively port audited display-backend logic: existing-mode HVS takeover first, then cursor and synchronization; investigate mode changes and monitor handling separately. | One hardware/regression report per enabled feature, validated resource/memory ownership, and a retained firmware-framebuffer fallback. Native mode setting requires its own HDMI/clock/resource audit. |
| **M5 — Stability, power, and recovery** | Complete lifecycle, cleanup, stop/start, supported power transitions, ownership release, repeated boot, and driver verification. Do not equate upstream blanking with implemented display power-down. | Repeatable stress and recovery results, no unresolved verifier findings in the tested configuration, and explicit unsupported/untested behavior. |
| **M6 — Packaging and release** | Harden reproducible CI, signing/package checks, symbols, hashes, compatibility records, and installation/removal documentation. Plan the appropriate production-signing path separately from test signing. | A tested package that can be installed, identified as active, and removed, with matching source/toolchain/firmware records and no private keys in the repository. |
| **M7 — Hardware rendering** | Evaluate the pinned V3D/WDDM work under a separate design: controlled off-screen jobs, memory/context management, submission/fences, isolation, timeout recovery, and API-specific user-mode graphics integration. | Correct hardware-rendered application output, supported APIs/features identified, multiple-client isolation, and recovery from forced GPU faults. A test job or declared WDDM version alone is not full acceleration support. |

**Current state:** M0 documentation is established. M0A has preliminary source findings only; the complete audit is pending. M1-M7 implementation and hardware validation are not started. M7 is a separate workstream and is not required to prove the first display-only prototype.

### Milestone details and stop conditions

**M0A — Audit before copying.** Produce a per-file manifest containing upstream URL, commit, path, blob/hash, license tag/notices, external dependencies, disposition, planned local destination, and local modifications. Include shared ReactOS headers, `HalGetCachedAcpiTable`/loader-related assumptions, debug/build helpers, compiler-specific ARM64 operations, shader provenance, and any user-mode dependencies. Verify Windows replacements rather than assuming similarly named interfaces are interchangeable. Pin the Microsoft sample revision separately. Keep each later import small and distinguish upstream content from local adaptation.

**M1-M3 — Prove the Windows handoff.** `ACPI\BCM2712` is a candidate identity from the prior firmware/source review, not proof of the device exposed by the installed UEFI binary. Inspect the live hardware IDs before writing the INF match. Use Windows-assigned resources; do not create a competing adapter for the same physical display. Validate framebuffer address, size, pitch, format, memory attributes, and lifetime. Treat absent/invalid boot-display information as a blocked bring-up item, not permission to guess a physical address. Keep render-engine resets and new hardware modes outside this stage.

**M4 — Introduce native display ownership incrementally.** First preserve the active firmware mode while validating the HVS display list and scanout mapping. Add cursor handling and synchronization only after that baseline works. Do not enable VSync, extra outputs, rotation, overlays, or mode changes merely because a field or callback exists upstream. Check the resources needed for HDMI timing, clocks, monitor identification, and hot-plug separately from the existing GPU resource list. Coordinate shared mailbox access with other platform drivers before using it.

**M5-M6 — Release evidence, not successful return codes.** Audit both the Microsoft sample's documented sleep limitation and the pinned ReactOS power implementation. Verify each supported transition on the actual platform, including restoration of mappings, display state, and queued-work cleanup. Record platform power states that cannot be supported or tested. Driver installation, active-driver selection, and successful presentation are separate checks. Preserve the Windows inbox driver and a tested recovery path.

**M7 — Rendering is not a version-constant change.** Start with controlled off-screen jobs and verified pixel results, then implement the required Windows memory, scheduling, synchronization, and recovery behavior. Audit command/buffer validation and process isolation before accepting untrusted application workloads. Provide the necessary user-mode component for each claimed graphics API; a kernel driver alone is not a complete Direct3D stack. Do not expose arbitrary physical-memory or MMIO access as a shortcut. Keep display-only and experimental rendering configurations independently testable.

## First implementation task

**Complete the reuse/dependency audit, then create the smallest Windows-native ARM64 display-only driver that presents an updating desktop using the Pi's existing boot framebuffer.**

The first implementation session should:

1. Review the pinned `rpi5vc4` baseline and create the M0A manifest. Mark each candidate **reuse after adaptation**, **rewrite Windows integration**, or **defer to rendering**. Resolve dependencies and license/provenance questions before importing implementation code.
2. Record the exact Windows build, UEFI and submodule revisions, board revision, monitor, HDMI port, and boot mode. Verify an independent recovery and diagnostic path.
3. Verify the display adapter's actual Windows hardware IDs and resources. Document the boot-display and resource contract with the existing UEFI project.
4. Pin the Microsoft KMDOD revision and matching SDK/WDK toolchain; create an ARM64 solution with a distinct driver/service identity and official Windows interfaces. Do not transplant ReactOS's full-WDDM `DriverEntry` into this KMDOD.
5. Add initialization/resource logging, validate boot-display ownership and framebuffer information, and implement the minimum synchronous presentation path. Keep native mode changes, V3D execution, and hardware-cursor changes out of this first prototype.
6. Test on the recoverable physical Pi and retain active-driver identity, initialization results, presentation counters, and rollback evidence. Record blockers rather than adding hard-coded addresses or unsupported capability claims.

Do not advertise the first desktop prototype as a stable release. This sequence is an implementation plan, not a promise that every gate can be completed in one session.

## Planned development requirements

- A development host with an ARM64-capable Microsoft driver build environment and matching Windows SDK/WDK versions.
- A dedicated Raspberry Pi 5 test installation with known-good firmware and a recoverable Windows image.
- A working HDMI monitor, adequate cooling, and a reliable power supply.
- An independently verified diagnostic connection; firmware UART output alone must not be assumed to provide Windows kernel debugging.
- A documented test-signing and installation procedure appropriate to the test machine's security configuration.

Toolchain versions and commands will be added after a real build is verified. There is no build or install command for this documentation-only state.

## Safety and implementation rules

- Never overwrite or rename Windows' inbox `BasicDisplay.sys`; use `Rpi5Display.sys` and preserve a recovery route.
- Validate framebuffer addresses, dimensions, pitch, pixel format, mapping bounds, and arithmetic. Review ARM64 cache attributes and coherency explicitly; distinguish ordering barriers from any required cache maintenance.
- Use Windows-assigned resources and verified device identity rather than hard-coded assumptions from a different firmware or operating system.
- Do not claim unsupported modes, interrupts, power states, WDDM capabilities, or GPU acceleration.
- Audit the KMDOD sample's known limitations and the upstream driver's incomplete behavior before adapting either for a release.
- Coordinate access to shared resources, including firmware-mailbox transactions, before introducing hardware-control paths that could conflict with other drivers.
- Keep firmware, toolchain, operating-system, sample, and imported-source revisions traceable. Change one major variable at a time during bring-up.
- Test only on a recoverable development system. Do not upload credentials, signing private keys, or unreviewed memory dumps to GitHub.
- Preserve upstream authorship and per-file notices. No bulk code import or silent dependency update without the source audit and a reviewable change.

## Validation and release criteria

The following are proposed project gates, not a claim of Microsoft certification:

| Check | Proposed minimum |
| --- | --- |
| Source import | Every imported file has an exact origin/revision, license/notices, dependency disposition, and adaptation record |
| Boot reliability | 20 successful cold starts and 20 restarts on each claimed baseline |
| Desktop workload | Eight hours of mixed activity without display corruption or a driver crash |
| Installation and rollback | Repeated installation/removal with confirmed return to a known-good display driver |
| Monitor and power behavior | Repeatable results for every reconnect, mode, or power transition claimed as supported |
| Driver verification | No unresolved violations in the recorded test configuration |
| Rendering milestone only | Verified hardware execution and pixel results, API-specific application tests, isolation, and forced-fault recovery |
| Evidence | Source/toolchain/firmware/Windows revisions, package hashes, logs, and known limitations retained |

Record unsupported or untested configurations explicitly. GitHub Actions can validate builds and packaging, but a CI success must not be reported as a Raspberry Pi hardware test.

Performance comparisons must use the same hardware, resolution, firmware, Windows build, and workload. Do not assume that replacing a framebuffer driver automatically improves rendering speed.

## Future source layout

Only `README.md` and `LICENSE` exist now. After the source audit and platform investigation, a possible structure is:

```text
rpi5-windows-display-driver/
    README.md
    LICENSE
    driver/                 # Windows ARM64 miniport and separated display backend
    package/                # INF and packaging inputs
    docs/                   # Source audit, platform contract, design and recovery
    tests/                  # Test procedures and non-sensitive results
    scripts/                # Reproducible build and diagnostic helpers
    .github/workflows/      # Build and package checks; not hardware validation
```

Planned documentation includes `docs/source-audit.md`, `docs/platform-contract.md`, `docs/windows-integration.md`, and `docs/test-plan.md`. Add a source-provenance/notices record before third-party implementation enters the tree. These directories, workflows, and additional files are planned, not implemented by this roadmap update.

Keep documentation, source imports, Windows adaptation, firmware changes, and hardware results in separately reviewable changes. Do not mark a milestone complete solely because its code was committed.

## References

### Windows driver foundation

- [Microsoft KMDOD source and README](https://github.com/microsoft/Windows-driver-samples/tree/main/video/KMDOD) — pin a specific revision during M0A/M2.
- [KMDOD sample documentation and limitations](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/kernel-mode-display-only-miniport-driver-kmdod-sample/)
- [Windows Driver Kit setup](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
- [Display-only driver initialization](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nf-dispmprt-dxgkinitializedisplayonlydriver)
- [Adapter device information](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_get_device_information)
- [Boot-display ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_acquire_post_display_ownership)
- [Display-only presentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_presentdisplayonly)
- [Driver Verifier](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/driver-verifier)
- [User-mode display drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/user-mode-display-drivers)

### Platform and hardware investigation

- [Existing Raspberry Pi 5 UEFI project](https://github.com/farjanatech/rpi5-uefi)
- [Pinned ReactOS Pi-specific display/GPU source](https://github.com/ahmedarif193/reactos/tree/b2b6c62a133053c8b3749febace8ce5a8e634936/drivers/directx/rpi5vc4)
- [Moving upstream branch, for explicit future comparisons only](https://github.com/ahmedarif193/reactos/tree/main-nt10/drivers/directx/rpi5vc4)
- [Pinned external miniport/shader source directory](https://github.com/ahmedarif193/reactos/tree/b2b6c62a133053c8b3749febace8ce5a8e634936/win32ss/drivers/miniport/rpi5vc4)
- [ReactOS generic software-display component](https://github.com/ahmedarif193/reactos/tree/main-nt10/drivers/directx/softgpu) — background reference, not the Pi-specific implementation.
- [Linux V3D documentation](https://docs.kernel.org/gpu/v3d.html)
- [Mesa V3D documentation](https://docs.mesa3d.org/drivers/v3d.html)

Reference repositories are research inputs, not project dependencies already imported or evidence that this driver works on Windows. Source comments, capability declarations, successful builds, and measured hardware behavior must remain distinct in project reports.

## Licensing and provenance

The repository already includes the GNU GPL version 3 license text in [`LICENSE`](LICENSE). This roadmap update does not modify that file or select a different project license. No third-party driver implementation is included in this documentation-only project.

The reviewed upstream files have differing license tags: `rpi5vc4.c` is marked `GPL-2.0-or-later`; `rpi5vc4_scanout.c` is marked `GPL-3.0-or-later`. Do not replace those original notices with a blanket label. Review the license and origin of each proposed import, its dependencies, and shader material before reuse; document any compatibility or distribution questions before code is included. Include the Microsoft sample's applicable notices when adapting it as well.

For each imported file, retain the author/copyright notices, original license, upstream repository/path, exact commit and content hash, and a record of local modifications. A project's top-level license file is not a substitute for this provenance record.

---

Project planning document prepared on **September 26, 2026** and revised the same day following the pinned `rpi5vc4` source review. This revision updates the roadmap and source-reuse plan only; development, full dependency/license auditing, and hardware validation remain pending.
