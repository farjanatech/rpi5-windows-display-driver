# sub-1st-hzfix: first hardware VSync / refresh-rate candidate

Status: **development branch only; not hardware-validated and not merged to `main`.**

This branch addresses the Windows KMDOD VSync contract that the 0.1.8 timing/EDID candidate did not implement. Microsoft requires a display-only driver that reports real signal frequencies to supply `DxgkDdiControlInterrupt`, `DxgkDdiGetScanLine`, `DxgkDdiInterruptRoutine`, and `DxgkDdiDpcRoutine` together.

## Design boundaries

The branch preserves the existing firmware-framebuffer `PresentDisplayOnly` path. It does not take over HDMI, HVS mode programming, V3D rendering, or the firmware-selected mode.

When the exp0.7 `Rpi5DisplayHandoff` variable is present and valid at DriverEntry:

1. `DxgkDdiGetScanLine` and `DxgkDdiControlInterrupt` are registered as a pair.
2. StartDevice revalidates the handoff against the exact POST framebuffer.
3. HDMI0 firmware display ID 2 maps to BCM2712 PixelValve0; HDMI1 display ID 7 maps to PixelValve1.
4. The driver finds that PixelValve only through the translated Windows resource list and maps exactly its assigned 0x100-byte MMIO resource.
5. The driver verifies the PixelValve is already scanning out.
6. The existing firmware timing supplies pixel clock / horizontal total / vertical total.
7. A real PixelValve VFP-start status edge anchors vertical blanking.
8. When Windows enables CRTC VSync through `DxgkDdiControlInterrupt`, the driver enables only the PixelValve VFP-start interrupt.
9. The ISR acknowledges the real hardware bit and reports `DXGK_INTERRUPT_DISPLAYONLY_VSYNC` to dxgkrnl.
10. `DxgkDdiGetScanLine` derives current progressive scan-line phase from the latest hardware VSync anchor and the validated timing.

If the exp0.7 handoff is absent at DriverEntry, the optional VSync callback pair is not registered and the old unspecified-frequency fallback is preserved.

## Safety properties

- No hard-coded refresh rate.
- No guessed framebuffer address.
- No mapping of a PixelValve unless Windows assigned the exact BCM2712 PixelValve resource.
- No HVS/HDMI/V3D register programming.
- No mode change.
- No enabling of unrelated PixelValve interrupts.
- VSync reporting is disabled and the MMIO mapping is released on stop.
- Failure to establish the VSync hardware path prevents the VSync-advertising configuration from starting instead of silently returning invented data.

## Required hardware test

Use the current exp0.7 UEFI release and keep the working 0.1.8 package plus the prior UEFI available for recovery.

Required evidence before merge:

- device starts with ProblemCode 0;
- desktop remains visible and `PresentDisplayOnly` continues advancing;
- diagnostics show `FirmwareTimingValid=1`;
- diagnostics show `VSyncAdvertised=1` and `VSyncHardwareReady=1`;
- PixelValve index is 0 for firmware display ID 2 or 1 for display ID 7;
- VSync interrupt counter advances while enabled;
- scan-line query counter advances;
- Advanced Display reports a numeric refresh derived from the firmware timing;
- EDID remains valid when the monitor firmware supplies a complete valid chain;
- Wi-Fi, fan/temp, NVMe, microSD and USB remain healthy;
- no Code 12, Code 43, TDR, display loss or boot regression occurs.

A hosted CI pass proves compile/package/static regression success only. It cannot validate physical interrupt routing, PixelValve state, monitor timing, or Windows runtime behavior on the Raspberry Pi 5.
