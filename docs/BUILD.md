# Build and package validation

## Scope

This builds an **experimental ARM64 KMDOD**. It is not an installed or hardware-tested driver. Hosted GitHub Actions is the reference automation path; no self-hosted Pi runner is configured by this project.

## Toolchain

The scripts acquire and verify these signed NuGet packages:

| Package | Pinned version |
| --- | --- |
| Microsoft.Windows.WDK.ARM64 | 10.0.26100.6584 |
| Microsoft.Windows.WDK.x64 | 10.0.26100.6584 |
| Microsoft.Windows.SDK.CPP | 10.0.26100.1 |

The WDK's ARM64 libraries and headers are used for the target; x64 packaging tools execute on the Windows x64 build host. SDK/WDK build families are both 26100. The build records exact NuGet content hashes and the selected MSVC version. MSVC is supplied by Visual Studio 2022 on `windows-2022`; the hosted runner image itself is not immutable, so record its image version from the run log. Do not describe the signed output as bit-for-bit reproducible: the disposable certificate and signature are intentionally different between runs.

Microsoft documents the VS2022/WDK 26100.6584 combination as an alternative to its newer VS2026 toolchain: [WDK setup](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk), [WDK NuGet](https://learn.microsoft.com/en-us/windows-hardware/drivers/install-the-wdk-using-nuget).

## GitHub Actions

Every push and pull request triggers `.github/workflows/ci.yml`. Manual dispatch is available when the workflow exists on the default branch. Permissions are read-only; it does not push releases or execute on the Pi.

The workflow runs:

1. `clang` C11 tests with `-Wall -Wextra -Werror` and ASan/UBSan on Ubuntu.
2. PowerShell syntax validation.
3. Original kernel C compilation for ARM64, warnings-as-errors, with WDK callback declarations checking signatures. A documented warning suppression is scoped only to vendor headers.
4. Native ARM64 link, imported-module inspection, NX/ASLR/integrity and no-RWX section checks.
5. INF validation and catalog generation.
6. Embedded SYS and catalog test-signatures, Authenticode checks, SHA-256 package manifest and detached CMS signature.
7. Negative package tests: changed SYS/INF/CAT/CER, modified manifest/signature, wrong commit/signer and extra package files must be rejected.

Debug and Release packages are uploaded only after the Windows job completes successfully. The portable job must also pass before treating the workflow as validated. Failed builds retain diagnostics, not a release package.

## Local build

Use a Windows x64 development host with Visual Studio 2022 C++ ARM64 cross tools, PowerShell 7, NuGet CLI, Python 3 and Git on PATH. Review/accept the applicable Microsoft toolchain licenses. From the source checkout:

```powershell
.\scripts\Build.ps1 -Configuration Debug -TestSign
.\scripts\Build.ps1 -Configuration Release -TestSign
```

This is a command-line WDK build; it does not require a WDK Visual Studio extension. The scripts do not install the built driver. Downloads live in `.tools/`, results in `out/ARM64/<configuration>/`. These paths are ignored by Git.

Portable tests can also be run on Linux:

```sh
mkdir -p out
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -g tests/framebuffer_test.c -o out/framebuffer_test
./out/framebuffer_test
```

## Artifact identity

Each configuration's output contains a `package` directory with exactly:

```text
Rpi5Display.sys
Rpi5Display.inf
Rpi5Display.cat
Rpi5Display.cer
```

The configuration root also includes `manifest.json`, detached `manifest.p7s`, a PDB, logs and lab helper scripts. The manifest binds the source commit and SHA-256 hashes of all four package files to the disposable build signer. Pin the full commit and certificate thumbprint **from the independently inspected GitHub run**, not from an untrusted downloaded JSON alone.

The private signing key is non-exportable and removed after the job; only the public certificate is distributed. Certificates are short-lived lab identities, not Microsoft production signatures. Expired packages must be rebuilt, not installed with disabled checks. Trusting a lab certificate or enabling test signing changes a machine's security posture; see the lab runbook.

## What the tests do not prove

These checks do not validate actual Pi hardware identity, ACPI boot-display association, the firmware framebuffer's lifetime and cache attributes, Windows mode-negotiation behavior, correct physical output, verifier cleanliness, power recovery or application acceleration. The non-hardware CI runner cannot supply those results. Preserve the source commit, run URL and platform evidence for every physical test.
