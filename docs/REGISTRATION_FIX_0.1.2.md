# 0.1.2: required registration callback, not a speculative WDDM upgrade

## Observed failure

The 0.1.1 package from commit `4aaa56c1fc1c398501350e391ad234b3c2d92d84` was staged and selected on the Windows 11 ARM64 test Pi. Its trace reached `DriverEntry version=0.1.1 status=0xc0000059`, with device Code 37. No successful AddDevice/StartDevice/framebuffer/present sequence was recorded. The failed package was subsequently uninstalled. Private user logs and Windows binaries are not included in this repository.

`STATUS_REVISION_MISMATCH` describes the return value, not necessarily a version-number defect. Changing a WDDM version constant based only on that name would be speculative.

## Confirmed source defect and interoperability analysis

The initialization table omitted **`DxgkDdiDispatchIoRequest`**, leaving that pointer NULL. Windows requires a dispatch entry even when a driver does not implement legacy video requests.

Two independent inspection steps distinguish this from an ABI-version guess:

1. The actual 0.1.1 ARM64 binary's linked Displib routine accepts its existing `0x300E` version and continues to the graphics-kernel initialization call. The version constant in the compiler settings and the one used by DriverEntry already agree.
2. A Microsoft-signed ARM64 `dxgkrnl.sys` reference in the Windows 22621 family checks the initial KMDOD table. Its symbol `DpiKmdDodInitialize` checks the dispatch pointer at table byte offset `0x28` and takes the `0xC0000059` error path when it is NULL. Its version comparison accepts `0x300E`. Other initial required entries in the old table are present.

Reference identity used for analysis (a reference image, **not asserted to be the user's exact installed file**):

| Item | Value |
| --- | --- |
| File version | `10.0.22621.4111` |
| Machine | ARM64 (`0xAA64`) |
| Microsoft symbol-server image | `https://msdl.microsoft.com/download/symbols/dxgkrnl.sys/1ECA7FD646a000/dxgkrnl.sys` |
| Downloaded SHA-256 | `2ee2879a73fa553a1cd0e6637afd20208a1fa79f92fce408f73174d64a953ff7` |
| Public symbols | `https://msdl.microsoft.com/download/symbols/dxgkrnl.pdb/90D90E2EE5BE5A3F5F0FF162FF6E5CCE1/dxgkrnl.pdb` |
| KMDOD initialization RVA | `0x27EE0` |
| Dispatch-pointer load/check RVAs | `0x27F20` / `0x27F24` |
| Missing-entry branch / status literal RVAs | `0x46E40` / `0x46E68` |
| Authenticode inspection | Valid Microsoft Windows signature, checked in the dedicated Windows analysis runner |

A public Winbindex index was used only to locate the symbol-server image. Its indexed hash differs from the symbol-server variant; it was not falsely treated as a hash match. PE identity and Microsoft's Authenticode verification were checked, and the actual downloaded hash is recorded above. The analysis workflow failed later while locating the separate BasicDisplay reference; that later failure does not change the completed dxgkrnl inspection, but the entire investigation run must not be described as green.

The required pointer's `0x28` position is also checked against the actual ARM64 WDK structure at compile time. There is no runtime patching of Windows, custom replacement of Displib, copying of Microsoft implementation code, or bypass of the graphics-kernel guard.

## Correction

- Add the official `DXGKDDI_DISPATCH_IO_REQUEST` entry, with the WDK checking its function signature. The implementation returns `STATUS_NOT_SUPPORTED` without accessing request buffers or adding an arbitrary-memory/IOCTL interface.
- Use one callback-binding list in the actual driver and the logical regression test. Before registration, diagnose any missing initial required entry explicitly.
- Keep the existing display-only `DXGKDDI_INTERFACE_VERSION_WIN8` ABI (`0x300E`). Assign `init.Version` from the compile-time interface macro and assert the intended ABI and ARM64 pointer size. No render capability, higher WDDM feature set, interrupts or VSync is added.
- Record compiled/requested interface, initialization-table size, dispatch offset, Windows version and actual registration return status in the kernel ETW/debug log. Add an AddDevice-entry marker.
- Record installed graphics-kernel/BasicDisplay version and hash metadata in support bundles, without collecting those binaries.
- Check hibernation before the check-only installer path returns success. The check remains read-only. Missing/unreadable evidence fails rather than silently passing.
- Report absent optional PnP properties as absent instead of repeatedly dereferencing a nonexistent `Data` property on an unbound device.

## Regression and build checks

`tests/registration_test.c` consumes the same binding names as DriverEntry. It verifies that the dispatch entry is bound, recreates the 0.1.1 omission, and individually removes each of the eleven initial required entries to ensure detection. This is a **logical table test**, not execution of the Windows graphics kernel or an imitation of WDK structure layouts.

The ordinary Debug/Release ARM64 WDK builds, PE/INF/catalog/signature tests, portable framebuffer sanitizers, package integrity tests, CMD tests and Windows PowerShell 5.1 support-bundle tests remain required. Additional power-check tests cover disabled/enabled/absent/unreadable hibernation evidence without modifying the build host's power policy.

Compilation and these tests cannot prove actual Windows registration, framebuffer ownership, display output, rollback or power/stress reliability. The next physical test must show `DriverEntry version=0.1.2 status=0x00000000`, subsequent lifecycle markers and, separately, successful framebuffer handoff/present activity. A new downstream failure is a new diagnostic boundary, not a passed desktop milestone. Issue #2 remains open.

## Next test

Use the new 0.1.2 CMD package in a fresh folder; do not mix old 0.1.1 scripts/binaries. The user has already completed power preparation and removed the failed package. No repeated disk preparation, Windows reinstall or blanket security changes are required for this fix.

On the recoverable Pi test installation, run `Install.cmd`, review its checks and type `INSTALL`. Keep the independent recovery route available because a driver that now passes registration will reach previously untested startup code. Send the resulting support ZIP. If it fails or asks for a restart, retain the evidence before making further changes. No automatic reboot or remote upload is performed.

## Public interface references

- [KMDOD initialization structure and version](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_kmddod_initialization_data)
- [DispatchIoRequest signature, return contract and IRQL](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkddi_dispatch_io_request)
- [Display-only registration function](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nf-dispmprt-dxgkinitializedisplayonlydriver)
