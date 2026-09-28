/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../core/vsync_timing.h"

int main(void)
{
    const uint64_t qpc_hz = 148500000ULL;
    const uint64_t anchor = 1000000ULL;
    const uint32_t clock_khz = 148500;
    const uint32_t htotal = 2200;
    const uint32_t vdisplay = 1080;
    const uint32_t vtotal = 1125;
    uint32_t line = 0;
    int blank = 0;

    assert(rp_vsync_timing_valid(clock_khz, htotal, vdisplay, vtotal));

    /* VFP_START is the first blanking line. */
    assert(rp_vsync_scanline_from_qpc(qpc_hz, anchor, anchor,
        clock_khz, htotal, vdisplay, vtotal, &line, &blank));
    assert(line == 1080 && blank);

    /* Last blanking line. */
    assert(rp_vsync_scanline_from_qpc(qpc_hz, anchor,
        anchor + 44ULL * htotal,
        clock_khz, htotal, vdisplay, vtotal, &line, &blank));
    assert(line == 1124 && blank);

    /* After all 45 blanking lines the next active frame starts at line zero. */
    assert(rp_vsync_scanline_from_qpc(qpc_hz, anchor,
        anchor + 45ULL * htotal,
        clock_khz, htotal, vdisplay, vtotal, &line, &blank));
    assert(line == 0 && !blank);

    assert(rp_vsync_scanline_from_qpc(qpc_hz, anchor,
        anchor + (45ULL + 100ULL) * htotal,
        clock_khz, htotal, vdisplay, vtotal, &line, &blank));
    assert(line == 100 && !blank);

    /* Full-frame wrap returns to the same VFP-start anchor. */
    assert(rp_vsync_scanline_from_qpc(qpc_hz, anchor,
        anchor + (uint64_t)vtotal * htotal,
        clock_khz, htotal, vdisplay, vtotal, &line, &blank));
    assert(line == 1080 && blank);

    assert(!rp_vsync_scanline_from_qpc(0, anchor, anchor,
        clock_khz, htotal, vdisplay, vtotal, &line, &blank));
    assert(!rp_vsync_scanline_from_qpc(qpc_hz, 0, anchor,
        clock_khz, htotal, vdisplay, vtotal, &line, &blank));
    assert(!rp_vsync_timing_valid(clock_khz, htotal, vdisplay, vdisplay));

    puts("PASS: hardware-VSync scanline phase and validation");
    return 0;
}
