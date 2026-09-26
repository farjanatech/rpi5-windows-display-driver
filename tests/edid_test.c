/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../core/edid.h"

static const unsigned char dtd1080p60[18] = {
    0x02,0x3a, 0x80,0x18,0x71, 0x38,0x2d,0x40,
    0x58,0x2c,0x45,0x00, 0x00,0x00,0x00,0x00,0x00,0x1e
};

static void checksum(unsigned char *block)
{
    unsigned i, sum = 0;
    block[127] = 0;
    for (i = 0; i < 127; ++i) sum += block[i];
    block[127] = (unsigned char)(0u - (sum & 0xffu));
}

static void base(unsigned char *edid, unsigned extensions)
{
    static const unsigned char hdr[8] = {0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00};
    memset(edid, 0, RP_EDID_MAX_BYTES);
    memcpy(edid, hdr, sizeof(hdr));
    edid[18] = 1; edid[19] = 4;
    edid[126] = (unsigned char)extensions;
}

int main(void)
{
    unsigned char edid[RP_EDID_MAX_BYTES];
    RP_EDID_TIMING t;

    base(edid, 0);
    memcpy(edid + 54, dtd1080p60, sizeof(dtd1080p60));
    checksum(edid);
    assert(rp_edid_valid(edid, 128));
    assert(rp_edid_match_timing(edid, 128, 1920, 1080, &t));
    assert(t.active_width == 1920 && t.active_height == 1080);
    assert(t.total_width == 2200 && t.total_height == 1125);
    assert(t.pixel_rate_hz == 148500000u);
    assert(t.block_index == 0 && t.descriptor_offset == 54);
    assert((unsigned long long)t.pixel_rate_hz * 1000ull /
           ((unsigned long long)t.total_width * t.total_height) == 60000ull);

    edid[20] ^= 1;
    assert(!rp_edid_valid(edid, 128));
    edid[20] ^= 1;

    base(edid, 1);
    checksum(edid);
    edid[128] = 0x02; edid[129] = 0x03; edid[130] = 4; edid[131] = 0;
    memcpy(edid + 128 + 4, dtd1080p60, sizeof(dtd1080p60));
    checksum(edid + 128);
    assert(rp_edid_valid(edid, 256));
    assert(rp_edid_match_timing(edid, 256, 1920, 1080, &t));
    assert(t.block_index == 1 && t.descriptor_offset == 4);
    assert(!rp_edid_match_timing(edid, 256, 1280, 720, &t));

    edid[126] = 0;
    checksum(edid);
    assert(!rp_edid_valid(edid, 256));

    puts("PASS: EDID checksums, base/CTA DTD parsing and exact geometry matching");
    return 0;
}
