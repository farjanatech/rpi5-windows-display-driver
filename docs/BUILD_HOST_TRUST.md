# Temporary signing trust on the build host

`Build.ps1 -TestSign` requires an elevated **dedicated Windows build host**. GitHub's disposable `windows-2022` runner is the reference host. It creates one short-lived, non-exportable code-signing private key in the build user's personal certificate store, exports only its public certificate, and signs the lab SYS/CAT and manifest.

To execute SignTool's normal Authenticode verification, the build temporarily adds **only that newly generated public certificate** to the build machine's trusted-root store. The exact entry and private key are removed in `finally`. This is an explicit, temporary trust change on the build host, not a change to Secure Boot, HVCI, test-signing mode or any physical Pi.

The initial CurrentUser-root implementation blocked waiting for Windows' root-trust UI on a noninteractive runner. The bounded process log established that certificate insertion, rather than compilation or INF validation, was stalled. Elevated LocalMachine insertion avoids requiring that interactive desktop. Native validation tools now have 120-second timeouts; timed-out validation is a failure, never a pass.

For a persistent local build host, review this behavior before running `-TestSign`. Use a disposable VM where possible. If the build process is forcibly terminated before cleanup, inspect the exact generated thumbprint from that build's log and remove that specific test root/private key manually. Do not delete unrelated certificates or disable signature validation.

The target Pi uses a separate explicit lab installation and confirmation process in [LAB_INSTALL.md](LAB_INSTALL.md). Building or downloading an artifact never installs a driver on the Pi.
