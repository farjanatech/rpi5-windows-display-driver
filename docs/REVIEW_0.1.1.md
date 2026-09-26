# 0.1.1 source review and corrections

Reviewed baseline: `1299b166f0f129c123ed5c3f58be00921c461c89`. Scope: existing firmware-framebuffer prototype, validators and lab deployment. Findings are from source review and software tests; no physical Pi validation is implied.

## Corrected or hardened

1. **Pending-reboot installation:** the old installer cleared `LabEnable` whenever it still saw another service, including PnPUtil exit 3010. That could block the deliberately selected driver at the pending reboot. Pending reboot now retains the explicit opt-in; staging without selection clears it. A tested decision helper distinguishes the cases.
2. **Same service is not the same binary:** the old installer could report selection based on the service name even when Windows retained an older Rpi5Display package. The new path validates the selected INF and on-disk SYS hash against the authenticated manifest and requires a known zero problem code, except for a separately reported pending restart.
3. **Incomplete diagnostics:** original `DbgPrintEx` messages needed debugger capture, and native PnP output was not consistently transcript-friendly. New self-describing kernel ETW tracing and CMD-launched pre/post collection preserve statuses, geometry, mode negotiation, sampled presentations and Windows setup evidence. The provider unregisters on failed DriverEntry and normal unload; no ETW calls were added to the crash-display path.
4. **Handoff identity:** only logical target 0 is exposed, but the old handoff copied the previous firmware target ID unchanged and omitted its ACPI identifier from the child descriptor. Handoff now returns logical target 0, carries the actual acquired ACPI ID into child reporting, and validates that an active mapping exists before returning it.
5. **Source-mode contract:** source validation checked dimensions/format but not row stride or accepted color basis. It now rejects unsupported color bases and invalid/overflowing layouts rather than committing a mode the presentation core cannot consume.
6. **Stop/presentation robustness:** admission rechecks `Active` after waiting for the mutex; failed present batches request a full shadow resynchronization, including validation failures rather than only exceptions.
7. **Device-state access:** the writer now requests only query/set permissions and allows creation through Configuration Manager for an explicitly requested device-state write. Read-only accesses do not create a key. Stage/log paths reject reparse ancestors and use restricted project-owned directories.
8. **Malformed binary validation:** the original PE validator did not bound import descriptors to the declared directory and could inspect virtual-only/out-of-file data. The new bounded parser rejects malformed headers, section tables, entrypoints, directories and names. Thirteen mutations of the actual built image exercise its failure paths.
9. **Operational delivery:** complete CMD launchers, signed-package pinning, local logs even on ordinary failures, bounded native commands, recovery-state retention and a ready-to-extract Debug installer artifact are added. CI packages depend on the portable tests passing.

## Investigated, not asserted as fixed bugs

The initial review suspected that `DXGK_DRIVERCAPS.WDDMVersion == 0` was a mismatch with the WIN8 KMDOD registration. Microsoft's general feature table asks for the version, but the detailed DXGK_DRIVERCAPS documentation says the member is reserved/zero for interface versions at or above WIN7. Therefore it is **not classified as a confirmed defect and was not changed blindly**. The driver still declares the existing display-only callback contract and does not advertise unimplemented graphics features.

Primary references:
- [Detailed driver caps](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_drivercaps)
- [WDDM feature table](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-driver-and-feature-caps)
- [POST release contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkddi_stop_device_and_release_post_display_ownership)
- [Kernel TraceLogging lifecycle](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/tracelogging-for-kernel-mode-drivers-and-components)
- [IoOpenDeviceRegistryKey](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-ioopendeviceregistrykey)
- [CM_Open_DevNode_Key](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_open_devnode_key)
- [PnPUtil results and driver selection](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax)

## Validation scope

Portable ASan/UBSan checks cover framebuffer arithmetic, padded strides and 41,616 overlapping single-rectangle moves. Both ARM64 configurations compile with WDK callback declarations and `/W4 /WX`; PE/INF/catalog/signature/tamper tests run. New installer tests exercise decision states, argument quoting, CMD help, native stdout/stderr and timeout collection, and a synthetic **user-mode** ETW provider/ZIP. Synthetic events use a separate GUID and are never hardware evidence.

Actual device-key creation, driver load, cache/memory reservation and Windows mode negotiation, live driver ETW, first desktop, online/offline rollback, verifier/lifecycle/power reliability remain hardware tests. Sleep/resume, native HVS/HDMI mode programming and GPU acceleration remain unsupported. There is no claim that all defects have been found, and an automated installer does not remove these risks.
