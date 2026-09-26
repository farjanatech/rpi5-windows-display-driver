/* SPDX-License-Identifier: GPL-3.0-only
 * Pure timing helpers for the Raspberry Pi 5 display-only VSync path.
 */
#ifndef RP_VSYNC_TIMING_H
#define RP_VSYNC_TIMING_H

#if defined(_KERNEL_MODE)
typedef ULONGLONG rp_v_u64;
typedef ULONG rp_v_u32;
#else
#include <stdint.h>
typedef uint64_t rp_v_u64;
typedef uint32_t rp_v_u32;
#endif

static inline int rp_vsync_timing_valid(
    rp_v_u32 clock_khz,
    rp_v_u32 htotal,
    rp_v_u32 vdisplay,
    rp_v_u32 vtotal)
{
    return clock_khz != 0 &&
        htotal != 0 &&
        vdisplay != 0 &&
        vtotal > vdisplay;
}

/*
 * last_vblank_qpc is anchored at the PixelValve VFP_START interrupt, which is
 * the start of vertical blanking on the active progressive scanout.
 */
static inline int rp_vsync_scanline_from_qpc(
    rp_v_u64 qpc_frequency,
    rp_v_u64 last_vblank_qpc,
    rp_v_u64 now_qpc,
    rp_v_u32 clock_khz,
    rp_v_u32 htotal,
    rp_v_u32 vdisplay,
    rp_v_u32 vtotal,
    rp_v_u32 *scanline,
    int *in_vblank)
{
    rp_v_u64 pixel_hz;
    rp_v_u64 frame_pixels;
    rp_v_u64 frame_ticks;
    rp_v_u64 delta_ticks;
    rp_v_u64 phase_ticks;
    rp_v_u64 phase_pixels;
    rp_v_u64 line_advance;
    rp_v_u64 line;

    if (!scanline || !in_vblank || !qpc_frequency || !last_vblank_qpc ||
        now_qpc < last_vblank_qpc ||
        !rp_vsync_timing_valid(clock_khz, htotal, vdisplay, vtotal)) {
        return 0;
    }

    pixel_hz = (rp_v_u64)clock_khz * 1000ULL;
    frame_pixels = (rp_v_u64)htotal * (rp_v_u64)vtotal;
    if (!pixel_hz || !frame_pixels) {
        return 0;
    }

    /*
     * Round to the nearest performance-counter tick. The multiplication is
     * comfortably within 64 bits for the validated display timing bounds.
     */
    frame_ticks = (qpc_frequency * frame_pixels + pixel_hz / 2ULL) / pixel_hz;
    if (!frame_ticks) {
        return 0;
    }

    delta_ticks = now_qpc - last_vblank_qpc;
    phase_ticks = delta_ticks % frame_ticks;
    phase_pixels = (phase_ticks * pixel_hz) / qpc_frequency;
    line_advance = phase_pixels / htotal;

    /* VFP_START occurs immediately after the final active scan line. */
    line = ((rp_v_u64)vdisplay + line_advance) % vtotal;

    *scanline = (rp_v_u32)line;
    *in_vblank = line >= vdisplay;
    return 1;
}

#endif
