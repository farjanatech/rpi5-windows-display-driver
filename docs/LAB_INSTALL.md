# Lab preflight, installation and rollback

> **Experimental / no physical validation yet.** An installation may black-screen or crash the Pi. These scripts do not make an untested kernel driver safe for daily use. Use a dedicated, recoverable test image and an independent diagnostic path. Hosted GitHub Actions cannot install or test the driver on your physical Pi.

## 1. Verify the platform before changing anything

Use native ARM64 PowerShell on the Windows 11 Pi, not an emulated x86/x64 process. Obtain the scripts from a reviewed Git commit in this repository; never run arbitrary downloaded scripts as administrator.

```powershell
.\scripts\Collect-Platform.ps1 -OutputPath "$env:TEMP\Rpi5Display-platform.json"
```

The collector is read-only except for its local report. Record the exact source/UEFI revisions (including submodules), board revision, Windows build, HDMI connector, monitor and boot mode. Review/redact identifiers before sharing. There is no automatic upload.

The intended adapter must actually report `ACPI\BCM2712`. Do not force an unrelated generic/root-enumerated device or edit hardware IDs. Confirm firmware keeps the Windows-handed framebuffer reserved, valid and accessible after the OS handoff. Its dimensions, pitch, pixel layout, physical-memory lifetime and cache attributes require real-platform review; source files alone do not prove them.

## 2. Establish and test recovery

Before the first installation, retain a restorable disk image or second known-good boot device. Test access to Windows recovery and the boot medium without the experimental driver. Save BitLocker/device-encryption recovery information securely where applicable; never put recovery keys or memory dumps in GitHub.

Establish a working Windows kernel-debug/diagnostic connection. Firmware UART text alone is not proof that WinDbg can attach to Windows. Record `lm m Rpi5Display`, initialization status and presentation counters during testing. A screenshot or Device Manager name alone does not establish the active module or successful presentation.

Keep the known-good display package and the inbox Basic Display driver untouched. Do not copy a SYS over an inbox file, change inbox ownership/permissions, or remove unrelated driver packages.

## 3. Prepare the designated test system deliberately

This prototype does **not** implement physical adapter sleep/resume. Disable sleep and hibernation for the controlled test using the normal Windows power configuration, and record the original settings for restoration. Display blanking is not physical HDMI/PHY power-down. Do not interpret a power callback's return value as proof of hardware suspend.

Test-signed kernel code requires the appropriate test-signing boot configuration. Microsoft documents `bcdedit /set TESTSIGNING ON`, its required restart, possible Secure Boot/BitLocker restrictions and the requirement to sign the binary even with Memory Integrity/HVCI. Read the [official test-signing guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option) before changing that dedicated test installation. Do not disable protections indiscriminately to get a package to load.

**The installer does not change Secure Boot, HVCI, BCD, execution policy, power settings or firmware.** It checks the currently booted kernel's Code Integrity test-signing flag and stops if absent. It checks that hibernation is already disabled and requires the operator to confirm the remaining power-policy, recovery and diagnostic prerequisites. Some policies need a restart before they take effect. An expired lab certificate requires a fresh build.

## 4. Get one verified GitHub artifact

Choose a successful **ARM64 driver validation** run with both portable tests and ARM64 build jobs passing. Inspect its exact source commit and the certificate thumbprint printed in the **Debug** build log. Download and extract the matching `Rpi5Display-ARM64-LAB-<commit>` artifact. Use the Debug configuration for the first hardware test. Each configuration has its own disposable signer.

Pin the expected commit and certificate thumbprint from the trusted GitHub run, not merely from files inside a downloaded ZIP. Use deployment scripts from that reviewed repository commit. The artifact's four package files are hash-bound to a detached CMS-signed manifest; a self-consistent but untrusted manifest is not its own trust anchor.

The following example deliberately contains placeholders. Replace them with the exact inspected run and the **actual device instance ID** from your platform report:

```powershell
$params = @{
    ArtifactRoot       = 'C:\Lab\Rpi5Display\Debug'
    ExpectedCommit     = '<full 40-character source commit from the successful run>'
    ExpectedThumbprint = '<40-character Debug signer thumbprint from that run>'
    DeviceInstanceId   = '<exact ACPI device instance ID from your Pi>'
}
.\scripts\Install-Lab.ps1 @params
```

Without `-Install`, this is preflight only: it verifies the signed manifest, hashes, architecture, exact device and test-signing state without installing certificates, changing the lab gate or installing a driver. Fix the cause of any failure instead of removing the check. `-WhatIf` is also supported for the modifying path.

## 5. Explicit controlled installation

Proceed only after the baseline/recovery/diagnostic review. Open an elevated **native ARM64** PowerShell session and use the reviewed source scripts:

```powershell
.\scripts\Install-Lab.ps1 @params -Install `
    -RecoveryConfirmed -DiagnosticsConfirmed -PowerPolicyConfirmed `
    -AcknowledgeUntestedHardware
```

The script asks for confirmation, rechecks the authenticated package in an administrator/SYSTEM-only staging directory, exports the previous third-party package when applicable, and records changes in:

```text
C:\ProgramData\Rpi5Display\Lab\<session-id>\install-state.json
```

It imports only the pinned lab certificate into LocalMachine Root/TrustedPublisher if not already present, verifies SYS/CAT signatures, sets the selected device's `LabEnable` DWORD and invokes PnPUtil for this INF. That trust change is deliberate and recorded. Neither private keys nor signing credentials are installed.

Device state is accessed using documented Configuration Manager and kernel registry APIs, not hard-coded `Enum` registry locations. The INF defaults the opt-in to zero without overwriting an existing explicit value. Starting the miniport without the gate is rejected. This gate controls startup; clearing it does not itself unload an already active driver.

[PnPUtil does not force a lower-ranked driver onto a device](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax). A staged package is not proof of binding. No ranking or signing checks are bypassed. The script reports the selected service/INF and leaves any reboot to the operator. It does not claim hardware success.

## 6. Verify the actual result

Record the active INF, device problem code, loaded `Rpi5Display.sys` module, successful POST handoff dimensions/pitch and first presentation counter. Move windows, scroll and redraw content on the physical monitor. Validate colors, cursor, overlapping moves, visibility changes and padded stride. Compare the installed SYS with the manifest hash.

Stop on a black screen, bugcheck, timeout, corrupted output, resource mismatch, verifier error or unexplained state. Retain the exact package/commit and diagnostic evidence. Do not proceed to native HVS or V3D writes while this baseline is unproven. See [hardware validation](HARDWARE_VALIDATION.md).

## 7. Online rollback

Use the independent diagnostic/recovery channel if the main display is not usable. Determine the exact published `oemNN.inf` from `install-state.json` and PnP/DISM metadata. Never use wildcard removal or guess an OEM INF number.

```powershell
.\scripts\Remove-Lab.ps1 -DeviceInstanceId '<exact device instance>' -PublishedInf 'oemNN.inf'
.\scripts\Remove-Lab.ps1 -DeviceInstanceId '<exact device instance>' -PublishedInf 'oemNN.inf' -Remove
```

The first call is preflight. The second confirms package identity, clears the opt-in and requests uninstall of only that published package. It does not use `/force`, reboot automatically or remove inbox Basic Display. Confirm Windows has selected the known-good fallback; restart only when required and with the recovery path ready.

Once rollback is confirmed, review `NewlyTrustedStores` and `Thumbprint` in the saved state. Remove **only** trust entries newly added for this lab session, after checking that no other retained lab package needs that certificate. Restore the original power/security configuration deliberately. The removal script does not automatically change those settings or delete certificates.

## 8. Offline recovery when Windows cannot start

Boot a known-good recovery environment and identify the **actual Windows volume**; its drive letter may differ in recovery. Restore the saved image when that is the tested recovery route. Alternatively, inspect and remove the exact experimental third-party package with the supported [DISM offline driver-servicing commands](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/dism-driver-servicing-command-line-options-s14?view=windows-11).

The following is a template, not a command with a verified drive/INF for your Pi:

```text
dism /Image:<Windows-volume-root> /Get-Drivers /Format:Table
dism /Image:<Windows-volume-root> /Get-DriverInfo /Driver:<verified-oemNN.inf>
dism /Image:<Windows-volume-root> /Remove-Driver /Driver:<verified-oemNN.inf>
```

Verify that the original INF name is `Rpi5Display.inf` before removal. Do not delete `.sys` files or edit driver-store contents by hand. Clearing the lab gate alone is not an offline rollback; remove the bad package or restore the known-good image.
