/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../core/display_handoff.h"

static void checksum(unsigned char *b)
{
    unsigned i, sum = 0;
    b[127] = 0;
    for (i = 0; i < 127; ++i) sum = (sum + b[i]) & 0xffu;
    b[127] = (unsigned char)((256u - sum) & 0xffu);
}

int main(void)
{
    RP_DISPLAY_HANDOFF h;
    memset(&h, 0, sizeof(h));
    assert(sizeof(h) == 568u);
    h.signature = RP_DISPLAY_HANDOFF_SIGNATURE;
    h.version = RP_DISPLAY_HANDOFF_VERSION;
    h.size = sizeof(h);
    h.flags = RP_DISPLAY_HANDOFF_TIMING_VALID | RP_DISPLAY_HANDOFF_EDID_VALID;
    h.timing.clock_khz = 148500;
    h.timing.hdisplay = 1920; h.timing.hsync_start = 2008; h.timing.hsync_end = 2052; h.timing.htotal = 2200;
    h.timing.vdisplay = 1080; h.timing.vsync_start = 1084; h.timing.vsync_end = 1089; h.timing.vtotal = 1125;
    h.edid_block_count = 1;
    { unsigned char hdr[8] = {0,255,255,255,255,255,255,0}; memcpy(h.edid, hdr, 8); }
    checksum(h.edid);
    assert(rp_display_handoff_valid(&h, 1920, 1080));
    h.timing.clock_khz = 0; assert(!rp_display_handoff_valid(&h, 1920, 1080));
    h.timing.clock_khz = 148500; h.edid[10] ^= 1; assert(!rp_display_handoff_valid(&h, 1920, 1080));
    h.edid[10] ^= 1; assert(rp_display_handoff_valid(&h, 1920, 1080));
    h.timing.flags = RP_DISPLAY_TIMING_FLAG_INTERLACE; assert(!rp_display_handoff_valid(&h, 1920, 1080));
    printf("PASS: display handoff size, timing and EDID validation\n");
    return 0;
}
