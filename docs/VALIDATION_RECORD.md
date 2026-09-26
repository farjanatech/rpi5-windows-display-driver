# Initial build-validation record

Recorded September 26, 2026. **This record is build/package evidence, not physical Pi validation.**

## First fully successful pipeline

- Source commit: `9abf253f62834e78cfeb8b1d03d0dce7a830be19`.
- [GitHub Actions run 36211175385](https://github.com/farjanatech/rpi5-windows-display-driver/actions/runs/36211175385), PR-triggered with an explicit checkout of that exact source commit.
- Portable tests: success. **41,616 exhaustive valid single-rectangle moves** on an 8x8 padded/guarded framebuffer, plus layout rejection and differing-stride copy checks, under AddressSanitizer and UndefinedBehaviorSanitizer.
- Windows Debug ARM64: compilation, linking, PE architecture/import/protection checks, INF validation, catalog generation, test-signatures, detached manifest signature and package-tampering rejection tests passed.
- Windows Release ARM64: the same pipeline passed.
- Artifact upload: success.

[Artifact 10896140731](https://github.com/farjanatech/rpi5-windows-display-driver/actions/runs/36211175385/artifacts/10896140731)

Artifact name: `Rpi5Display-ARM64-LAB-9abf253f62834e78cfeb8b1d03d0dce7a830be19`.

Archive SHA-256:

```text
3aa32f3e6a9f6253f3f6b0ec9c5b07e2b1a8a80b4569ab2501350a867923f1c3
```

The downloaded archive was independently checked against GitHub's digest. Both configurations' package hashes and detached CMS signatures were verified again using Python/OpenSSL without executing the Windows binaries. That second check verifies artifact integrity, not hardware compatibility.

| Configuration | Certificate thumbprint | Signed SYS SHA-256 |
| --- | --- | --- |
| Debug | `6CD6C5C371FA2BA03AE9846D6EE7B90E0F3D87A4` | `DC396CCE15FC6C6C1FE1AD107D5DB41D7CA7951312F1A0658827E140E43AE370` |
| Release | `47CFBC728985FCDA8B22BAE7520EBA3C8B0974E3` | `9044095FA7C1A7D4B003A259F2CA7BBF0DD1EF1695F0CC66252B63B07670C410` |

These identities apply **only** to the named run/artifact. Other runs, including a rebuild of the same source, have different disposable signing identities. Never reuse a thumbprint or SYS hash with another artifact. Retention is 14 days; this artifact's recorded expiry is October 10, 2026. Later commits must pass their own CI run; this record does not transfer a pass to untested changes.

## Deployment tooling review

The installation path defaults to read-only preflight. Explicit installation verifies the exact device and signed package, requires operator confirmation, records changes, and uses normal Windows driver selection. If Windows retains another driver, the lab gate is cleared instead of leaving a latent opt-in. Unbound device properties are reported as absent rather than treating every missing INF property as an access failure; actual property-enumeration failures still stop the script.

Build-host test-signing temporarily changes only the dedicated build VM's trust store; see [BUILD_HOST_TRUST.md](BUILD_HOST_TRUST.md). This is distinct from the explicit target-machine lab installation. No driver was installed on a physical Pi during the GitHub-only implementation.

## Not established

The physical boot-display association, framebuffer reservation/cache contract, initial driver load, correct desktop, actual callback behavior, Windows/UEFI compatibility matrix, Verifier, repeated boot/stress, suspend/resume, native HVS/HDMI features and V3D acceleration remain unverified or unimplemented as stated in the roadmap.

[Issue #2](https://github.com/farjanatech/rpi5-windows-display-driver/issues/2) tracks the real M1/M3 platform and desktop gate. The next operator action is the read-only collector described in [LAB_INSTALL.md](LAB_INSTALL.md), not blind installation.
