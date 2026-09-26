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

static void init_1080p60(RP_DISPLAY_HANDOFF *h)
{
    static const unsigned char hdr[8] = {0,255,255,255,255,255,255,0};
    memset(h, 0, sizeof(*h));
    h->signature = RP_DISPLAY_HANDOFF_SIGNATURE;
    h->version = RP_DISPLAY_HANDOFF_VERSION;
    h->size = sizeof(*h);
    h->flags = RP_DISPLAY_HANDOFF_TIMING_VALID | RP_DISPLAY_HANDOFF_EDID_VALID;
    h->timing.clock_khz = 148500;
    h->timing.hdisplay = 1920;
    h->timing.hsync_start = 2008;
    h->timing.hsync_end = 2052;
    h->timing.htotal = 2200;
    h->timing.vdisplay = 1080;
    h->timing.vsync_start = 1084;
    h->timing.vsync_end = 1089;
    h->timing.vtotal = 1125;
    h->edid_block_count = 1;
    memcpy(h->edid, hdr, sizeof(hdr));
    checksum(h->edid);
}

int main(void)
{
    RP_DISPLAY_HANDOFF h;
    unsigned char *ext;

    assert(sizeof(h) == 568u);

    /* The handoff must be volatile and visible to both boot services and Windows runtime. */
    assert(rp_display_handoff_attributes_valid(
        RP_UEFI_VARIABLE_BOOTSERVICE_ACCESS | RP_UEFI_VARIABLE_RUNTIME_ACCESS));
    assert(!rp_display_handoff_attributes_valid(
        RP_UEFI_VARIABLE_NON_VOLATILE |
        RP_UEFI_VARIABLE_BOOTSERVICE_ACCESS | RP_UEFI_VARIABLE_RUNTIME_ACCESS));
    assert(!rp_display_handoff_attributes_valid(RP_UEFI_VARIABLE_RUNTIME_ACCESS));
    assert(!rp_display_handoff_attributes_valid(RP_UEFI_VARIABLE_BOOTSERVICE_ACCESS));

    init_1080p60(&h);
    assert(rp_display_handoff_valid(&h, 1920, 1080));

    h.timing.clock_khz = 0;
    assert(!rp_display_handoff_valid(&h, 1920, 1080));
    h.timing.clock_khz = 148500;

    h.edid[10] ^= 1;
    assert(!rp_display_handoff_valid(&h, 1920, 1080));
    h.edid[10] ^= 1;
    assert(rp_display_handoff_valid(&h, 1920, 1080));

    /* Base block declares one extension: partial publication must be rejected. */
    h.edid[126] = 1;
    checksum(h.edid);
    assert(!rp_display_handoff_valid(&h, 1920, 1080));

    h.edid_block_count = 2;
    ext = h.edid + RP_DISPLAY_EDID_BLOCK_SIZE;
    memset(ext, 0, RP_DISPLAY_EDID_BLOCK_SIZE);
    ext[0] = 0x02; /* CTA extension tag */
    checksum(ext);
    assert(rp_display_handoff_valid(&h, 1920, 1080));

    ext[20] ^= 1;
    assert(!rp_display_handoff_valid(&h, 1920, 1080));
    ext[20] ^= 1;
    assert(rp_display_handoff_valid(&h, 1920, 1080));

    h.timing.flags = RP_DISPLAY_TIMING_FLAG_INTERLACE;
    assert(!rp_display_handoff_valid(&h, 1920, 1080));

    printf("PASS: display handoff attributes, timing and complete EDID validation\n");
    return 0;
}
