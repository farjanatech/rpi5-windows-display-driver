# Initial source, dependency and license audit (M0A)

Audit date: September 26, 2026. Scope: the first firmware-framebuffer KMDOD only. **This is not a completed audit of all future HVS/V3D imports or a hardware certification.**

## Decision: original Windows integration, no sample-source import

The repository already contains GNU GPL version 3 text. New original implementation files use `GPL-3.0-only`. Microsoft's Windows-driver-samples repository declares MS-PL, not MIT. The FSF lists MS-PL as GPL-incompatible. The earlier proposed sample import is therefore replaced by original code using the documented Windows display-driver interfaces. No Microsoft KMDOD sample implementation was copied or vendored.

Primary references:
- [Microsoft sample repository license](https://github.com/microsoft/Windows-driver-samples/blob/main/LICENSE)
- [FSF classification of MS-PL](https://www.gnu.org/licenses/license-list.html#ms-pl)
- [Microsoft display-only initialization contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nf-dispmprt-dxgkinitializedisplayonlydriver)
- [KMDOD initialization structure and callback requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_kmddod_initialization_data)
- [Display-only presentation contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_presentdisplayonly)

SDK/WDK declarations, compiler tools and import/static libraries are acquired separately under Microsoft's terms, not relicensed as project source. Release qualification must include review of any redistributed toolchain-derived material. This audit makes no general legal claim about every future combination of dependencies.

## ReactOS reference retained for later hardware work

Pinned repository: `ahmedarif193/reactos`, branch examined `main-nt10`, immutable commit **`b2b6c62a133053c8b3749febace8ce5a8e634936`**.

[Component at the pinned revision](https://github.com/ahmedarif193/reactos/tree/b2b6c62a133053c8b3749febace8ce5a8e634936/drivers/directx/rpi5vc4).

| Reference area | Finding / treatment in this implementation |
| --- | --- |
| `rpi5vc4.c` / `rpi5vc4.h` | Actual registration is full WDDM via `DxgkInitialize`, despite older KMDOD comments. ReactOS platform/loader/private-present integration is not imported. This project's official KMDOD callback layer is original. |
| `rpi5vc4_scanout.c` | ACPI output/EDID code remains a reference only. It is marked GPL-3.0-or-later, unlike main source files marked GPL-2.0-or-later. No code imported. |
| `rpi5vc4_present.c` / `rpi5vc4_vidpn.c` | Existing implementations are not assumed to implement this project's Windows-only contract. The current shadow-buffer operations and mode negotiation are original. |
| HVS/CRTC/cursor/IOMMU helpers | Deferred until actual display ownership, translated resources, addressing/coherency and recovery are documented. Each selected module needs a dependency/register/concurrency/license audit before import. |
| Mailbox helpers | Deferred. Shared mailbox ownership, bounded timeouts and coordination with other drivers must be designed before any transaction. Current driver issues no mailbox calls. |
| WDDM/V3D execution and shaders | Deferred to M7. CMake references shader material outside this directory under `win32ss/drivers/miniport/rpi5vc4`; a directory-only copy would omit dependencies. No shader binaries imported. |
| Power handling | Upstream blanking is not physical PixelValve/PHY power-down. Current lab driver makes no physical suspend/resume support claim. |

Detailed pinned evidence remains in the [original source-review roadmap](ROADMAP.md). Do not silently follow a moving upstream branch or claim that source presence proves Windows/hardware compatibility.

## Current dependency map

```text
Original driver C -> official ntddk/dispmprt/d3dkmddi declarations
                 -> WDK displib and kernel runtime/import libraries
                 -> Windows ntoskrnl/HAL runtime (verified from linked PE)
Original core C  -> scalar types and ordinary RAM only
Original scripts -> Windows PnP/registry/certificate APIs, PowerShell/.NET
CI               -> pinned Actions, signed NuGet packages, recorded MSVC host tools
```

There is no required ReactOS kernel, custom Dxgk resolver, private shadow-present GUID, FreeLoader callback, arbitrary-address IOCTL, shader blob or render user-mode driver in the initial implementation.

## Audit gates for future changes

For every proposed imported file, record its immutable source URL, SHA, exact license expression/notices, transitive includes and external symbols, Windows replacement requirements, MMIO/DMA resources, cache/interrupt assumptions and test plan. Resolve dependencies and applicable licensing before merging. A GPL label on this repository alone does not prove compatibility of everything linked to it.

For every new advertised capability, map its WDK callback contract to an implementation and an actual hardware test. Compiling against newer declarations does not authorize advertising a higher WDDM feature level.
