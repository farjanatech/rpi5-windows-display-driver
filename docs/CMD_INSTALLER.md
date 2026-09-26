# Rpi5Display 0.1.8 candidate — CMD installer and diagnostic logs

**Experimental Windows 11 ARM64 display-only driver. Version 0.1.7 is the hardware-working framebuffer baseline; 0.1.8 adds real firmware timing/EDID consumption and is not yet hardware-validated. This is not a full accelerated graphics driver. A bad kernel driver can black-screen or crash Windows. Use a restorable test image and working independent recovery/debug access.**

## 0.1.8 timing/EDID candidate

This candidate keeps the 0.1.7 present/copy path unchanged. When paired with Raspberry Pi 5 UEFI exp0.7, it reads the volatile `Rpi5DisplayHandoff` runtime variable, validates the exact POST geometry and firmware timing, returns the monitor EDID through `DxgkDdiQueryDeviceDescriptor`, and reports the real pixel clock / totals / HSync / VSync instead of inventing 60 Hz. If the handoff is absent or invalid, it deliberately falls back to the 0.1.7 unspecified-timing behavior.

The paired firmware candidate is tracked in `farjanatech/rpi5-uefi#9`. Test exp0.7 first with the exp0.6 recovery image available, then install this driver. Confirm ProblemCode 0, a real Advanced Display refresh rate and the correct monitor EDID before using TestUFO/MotionMark as performance evidence.

Preflight remains read-only. Use this package in a **new extracted folder**, never mixed with files from an earlier driver package. Existing verified disk/power/security preparation does not need to be repeated just because the driver version changed.

## Extract and start

Download `Rpi5Display-CMD-Installer-<commit>` from a successful GitHub Actions run in `farjanatech/rpi5-windows-display-driver`. Compare the ZIP SHA-256 with the trusted run's artifact digest. A checksum inside an untrusted download is not an independent trust anchor.

```cmd
certutil -hashfile Rpi5Display-0.1.8-CMD-Installer.zip SHA256
```

Extract **the entire ZIP** to a new local folder, for example `C:\Rpi5Display-0.1.2`. Do not run CMD files inside the ZIP viewer or separate them from their support files. Run on the Windows Pi, not the x64 PC used to download it.

```cmd
cd /d C:\Rpi5Display-0.1.2
Install.cmd
```

Double-clicking `Install.cmd`, or right-clicking it and choosing Run as administrator, is also supported. It selects native Windows PowerShell, requests UAC, detects exactly one `ACPI\BCM2712` device, verifies the pinned package/manifest and collects baseline logs. It asks you to type **INSTALL** after showing the package identity and recovery/power prerequisites. It then starts tracing, attempts installation, verifies selection/disk hash, captures 30 seconds of activity and packages logs. Move windows and scroll during capture if the display remains usable.

CMD is the entry point; included PowerShell scripts perform certificate, PnP and structured logging operations. No Python, WDK, Git, separate PowerShell 7 or internet access is required on the Pi. ExecutionPolicy Bypass applies only to the launcher's own process, not permanently or against Group Policy. Review the package before granting administrator access.

## Prerequisites

The currently booted test Windows installation must allow test-signed kernel drivers. The installer does not disable Secure Boot/Memory Integrity, change BCD, modify power policy, flash firmware or reboot. It imports only the verified lab certificate when explicitly installing and records that trust change.

A missing test-signing configuration is a preparation issue; follow [Microsoft's guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option), not disabled signature checks. An expired lab signer requires a fresh build. Existing verified preparation need not be repeated for this update.

Physical adapter suspend/resume is not implemented. Sleep and hibernation must already be disabled for the controlled experiment and original settings recorded for restoration. Keep the monitor on the same firmware-selected HDMI port/mode during the first test. Never force an unrelated hardware ID, unsupported mode or driver ranking.

## Commands

| Command | Behavior |
| --- | --- |
| `Preflight.cmd` | Check prerequisites and write a support bundle without installing a driver/certificate. Some privileged diagnostics can be unavailable. |
| `Install.cmd` | UAC, validation, explicit INSTALL confirmation, traced installation and support ZIP. |
| `Collect-Logs.cmd` | No installation; capture 30 seconds of runtime activity and system diagnostics. Useful after reboot or a new fault. |
| `Uninstall.cmd` | Find the selected Rpi5Display OEM package, require REMOVE confirmation, remove only that verified package and collect logs. |

All launchers support `/?` without elevation or device changes. For a different capture duration, use the included backend from elevated native ARM64 PowerShell:

```powershell
.\Run-Lab.ps1 -Action Collect -CaptureSeconds 60
```

Allowed capture duration: 1–120 seconds. Install/remove serialize against each other. There is no unattended installation, scheduled task, remote-control agent or automatic upload.

## Log locations and contents

Elevated sessions create a timestamped folder and adjacent ZIP:

```text
C:\ProgramData\Rpi5Display\Logs\<timestamp>-<session-id>\
C:\ProgramData\Rpi5Display\Logs\<timestamp>-<session-id>.zip
```

The exact path is printed after **SUPPORT BUNDLE:**. A non-elevated preflight uses `%TEMP%\Rpi5Display-preflight-<id>`. Installation state and the authenticated package are separately retained under `C:\ProgramData\Rpi5Display\Lab\<session-id>\`. These directories are not cleaned automatically.

| Evidence | Purpose |
| --- | --- |
| `session.log`, `result.json`, optional `error.txt` | Stage, commit, outcome, exit code and failure details |
| `commands.jsonl` and command stdout/stderr | Arguments, timings, exit code, timeout or launch error |
| `before/` / `after/` | Platform, device IDs, resources, binding, problem codes, power/verifier settings and graphics-kernel file identity |
| `driver*.etl`, decoded XML when available | Registration, boot framebuffer, mode queries, sampled presents, failures, power and unload |
| `windows-logs/` | Selected last-24-hour event logs, bounded 2 MiB SetupAPI tail, available dxdiag, dump inventory only |
| `previous-sessions/` | Selected evidence from up to two recent sessions per category, capped at 128 MiB total |
| `bundle-hashes.json` | Transfer-integrity hashes/sizes, not an independent signature |

The kernel provider is `farjanatech.Rpi5Display`, GUID `{8d553ba8-56d6-47a4-a5fb-b567ec131eb3}`. Tracing uses a 64 MiB circular ETL and periodic flushing. Capture begins before installation; the collector stops only its own trace and attempts XML decoding. Preserve raw ETL if decoding is incomplete.

The old 0.1.0 binary has no provider. Version 0.1.1 has the known registration omission and must not be reinstalled. Events cannot be recovered retroactively if no trace was running. A crash/power loss may lose buffered events or prevent ZIP creation; after recovery run `Collect-Logs.cmd` and retain the earlier raw folder. No persistent boot autologger is configured. Early boot failures can still require WinDbg or a separately reviewed crash dump.

## Result meanings

| Exit code | Meaning |
| --- | --- |
| `0` | Software operation completed; inspect result.json. Not proof of correct physical display output. |
| `3010` | Windows requests restart; deliberate startup opt-in retained. No automatic reboot. |
| `20` | Package staged but not selected; no ranking bypass, lab gate cleared. |
| `2` | Operator declined or extracted package incomplete. |
| Other nonzero | Failed check/operation/launch/permission; inspect the logs. UAC refusal may precede full log creation. |

Service name alone is insufficient. The installer checks selected package, on-disk SYS hash and PnP problem code. These still do not prove that the loaded module is the new binary or that the physical screen is correct. Registration/lifecycle/present trace events and the screen provide separate evidence.

Use `Uninstall.cmd` when Windows remains usable through the monitor or independent channel. Staged-but-unselected packages may need lower-level `Remove-Lab.ps1` with a verified OEM INF. For a nonbooting system use the tested image restore or exact-package DISM recovery in `docs/LAB_INSTALL.md` in the source archive. Do not manually delete inbox BasicDisplay or driver-store files.

## Share evidence safely

Logs can contain machine/user names, paths, device identifiers and other drivers' events. Review before sharing. No automatic upload occurs; raw memory dumps, private keys, passwords and framebuffer pixels are not deliberately collected.

Send the support ZIP with what happened on the monitor and whether the fault occurred during install/reboot/scrolling. If installation fails or requests restart, retain the ZIP before further changes. A green build or completed collector does not mark a physical driver test as passed.
