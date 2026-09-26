# Rpi5Display 0.1.1 — CMD installer and diagnostic logs

**Experimental Windows 11 ARM64 display-only driver. No physical Pi validation has been performed by the hosted build. This is not a full accelerated graphics driver or a guarantee of a working display. A bad kernel driver can black-screen or crash Windows. Use a restorable test image and working independent recovery/debug access.**

## Extract and start

Download the `Rpi5Display-CMD-Installer-<commit>` artifact from a successful GitHub Actions run in `farjanatech/rpi5-windows-display-driver`. Compare the ZIP SHA-256 with the trusted run's artifact digest. Do not rely on a checksum or signer copied only from an untrusted download. On Windows:

```cmd
certutil -hashfile Rpi5Display-CMD-Installer.zip SHA256
```

Extract **the entire ZIP** to a local folder (for example `C:\Rpi5Display-Lab`). Do not run a CMD from inside the ZIP viewer, and do not move just one CMD file away from its support files. Run on your Windows Pi, not the x64 PC used to download it.

```cmd
cd /d C:\Rpi5Display-Lab
Preflight.cmd
Install.cmd
```

`Install.cmd` selects native Windows PowerShell, requests administrator approval through UAC, detects exactly one `ACPI\BCM2712` device, verifies the pinned package/manifest, collects baseline logs, and runs the guarded installer. It asks you to type **INSTALL** only after showing the package identity and listing the recovery/power prerequisites. Normal driver-selection and signature rules remain in force. If no matching device or multiple matching devices are present, it stops without installing.

CMD is the entry point; the included PowerShell scripts perform certificate, PnP and structured logging operations. No Python, WDK, Git, separate PowerShell 7 installation or internet access is required on the Pi. The launcher sets `ExecutionPolicy Bypass` **only for its own PowerShell process**, not permanently or against organizational Group Policy. Review the downloaded scripts before granting administrator access.

## Security and power prerequisites

The currently booted test Windows installation must allow **test-signed kernel drivers**. Secure Boot, organizational policy and encryption can affect preparation. `Install.cmd` does not disable Secure Boot/Memory Integrity, change BCD, turn off hibernation, flash firmware or reboot automatically. It imports only the verified lab certificate when explicitly installing and records the trust changes.

When preflight reports that test signing is unavailable, consult [Microsoft's test-signing instructions](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option). The documented command, **only on the dedicated test installation after reviewing recovery/security implications**, is:

```cmd
bcdedit /set testsigning on
```

A restart is required before that change takes effect. A Secure Boot-policy error is a preparation issue, not permission to ignore signing checks. Keep Memory Integrity enabled unless separate verified hardware/driver evidence establishes a need to change it. An expired lab signer requires a fresh build.

Physical adapter suspend/resume is not implemented. Sleep and hibernation must already be disabled for the controlled experiment; the original settings must be recorded for later restoration. Keep a monitor connected to the same firmware-selected HDMI port/mode during the first test. Do not force unsupported resolutions or devices.

## Commands

| Command | Behavior |
| --- | --- |
| `Preflight.cmd` | Verifies prerequisites without installing a driver or certificate. Writes a local support bundle; some privileged diagnostics may be unavailable. |
| `Install.cmd` | UAC, checks, before snapshot, explicit INSTALL confirmation, starts tracing, installs, verifies selection/disk hash, captures 30 seconds of activity, then packages logs. |
| `Collect-Logs.cmd` | No installation. Captures 30 seconds of current runtime activity plus system diagnostics and limited previous-session evidence. Use it after reboot or when reporting an issue. |
| `Uninstall.cmd` | Finds the currently selected Rpi5Display OEM package and requires REMOVE confirmation. Removes only that verified package, records logs, and does not reboot or remove unrelated certificates. |

`Install.cmd /?` and the other launchers' `/?` show help without elevation or device changes. To capture a different bounded duration, run the included backend from an elevated native ARM64 PowerShell session:

```powershell
.\Run-Lab.ps1 -Action Collect -CaptureSeconds 60
```

The allowed capture range is 1–120 seconds. Installation and removal serialize against each other; they cannot run concurrently through this launcher. There is no unattended installation, scheduled task, remote-control agent, or automatic upload.

## Where logs are saved

Elevated installation/collection/removal sessions create a timestamped folder and adjacent ZIP under:

```text
C:\ProgramData\Rpi5Display\Logs\<timestamp>-<session-id>\
C:\ProgramData\Rpi5Display\Logs\<timestamp>-<session-id>.zip
```

The exact `SUPPORT BUNDLE:` path is printed at completion. A non-elevated preflight uses `%TEMP%\Rpi5Display-preflight-<id>` instead. Driver installation state and the verified package are separately retained under `C:\ProgramData\Rpi5Display\Lab\<session-id>\`. These directories are not cleaned automatically.

| Evidence | Purpose |
| --- | --- |
| `session.log`, `result.json`, `error.txt` when relevant | Installer transcript, stage, source commit, exit code and failure details. |
| `commands.jsonl` in collection subfolders | Command, arguments, start time, exit code, duration, timeout or launch error. A failed command does not silently become a pass. |
| `before/` and `after/` | Platform/firmware/OS, device IDs, selected packages, resources, PnP problem codes, service state, power configuration and verifier settings. |
| `driver*.etl`, `driver*-events.xml` when decoding succeeds | New kernel TraceLogging provider messages: startup, boot-display geometry, capabilities queries, mode commits, sampled presents/timings, failed presents, power and unload. |
| `windows-logs/` | Last 24 hours of selected Windows event channels, a bounded 2 MiB SetupAPI tail, dxdiag and a minidump **filename/size inventory only**. |
| `previous-sessions/` | Up to two recent sessions per category, limited to selected logs/state/ETL and 128 MiB total; useful when a crash interrupted an earlier run. |
| `bundle-hashes.json` | Hashes and sizes to detect corruption while transferring the support bundle; not an independent trust signature. |

The driver provider is `farjanatech.Rpi5Display`, GUID `{8d553ba8-56d6-47a4-a5fb-b567ec131eb3}`. The trace is a 64 MiB circular ETL with periodic flushing. The collector starts before the installation attempt, closes only its own trace, and attempts XML conversion. Windows Performance Analyzer can inspect raw ETL when XML decoding is incomplete. No debugger is required to collect this ETW stream, but independent recovery/debug access remains a prerequisite for risky first installation.

The old 0.1.0 binary has no such ETW provider. A collector cannot retroactively recover events that were never captured. A crash or power loss may discard recent buffered events or prevent ZIP creation; after recovery, run `Collect-Logs.cmd` and retain the earlier raw folder. No persistent boot autologger is configured. Early boot failures may still require WinDbg or a separately reviewed crash dump.

## Interpret the result correctly

| Exit code | Meaning |
| --- | --- |
| `0` | Requested software operation completed; inspect `result.json` for whether it was preflight, log collection or exact on-disk package selection. **Not proof of a physical desktop.** |
| `3010` | Windows requested a restart; existing binding may remain until then. The deliberate startup opt-in is retained. Reboot only with recovery ready, then collect logs again. |
| `20` | Package staged but not selected by Windows; no forced ranking change, lab opt-in cleared. |
| `2` | Operator declined an action or incomplete extracted package. |
| Nonzero other | Failed check, operation, launch or permissions; inspect the transcript. UAC refusal can occur before the full log directory exists. |

Seeing a service name alone is insufficient: the installer now checks the selected package's actual on-disk SYS hash and PnP problem code. Even these checks do not prove the loaded module is the newly built image or that the physical monitor is correct. Runtime trace version/presentation messages and the actual screen complete that evidence.

For rollback, use `Uninstall.cmd` only when Windows remains usable through the monitor or independent channel. A staged-but-unselected package may require the lower-level `Remove-Lab.ps1` with its verified OEM INF. For a nonbooting Windows installation, use the tested image restore or exact-package DISM recovery in the source archive's `docs/LAB_INSTALL.md`. Do not delete inbox `BasicDisplay.sys` or edit the driver store by hand.

## Share evidence safely

Logs can contain machine/user names, paths, device IDs and events involving other drivers. Review before sharing. No logs are sent anywhere automatically. Raw memory dumps, private signing keys, passwords and framebuffer pixels are not deliberately collected. Do not publish a full crash dump in a public GitHub issue without a separate privacy review.

When reporting an issue, provide the support ZIP, what happened on the monitor, whether it occurred during install/reboot/scrolling, and the exact HDMI port and firmware revision. Do not mark hardware tests passed just because this package built or the collector completed.
