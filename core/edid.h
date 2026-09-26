/* SPDX-License-Identifier: GPL-3.0-only
 * Minimal EDID validation and detailed-timing parsing shared by kernel and
 * portable tests. It never invents a timing and never programs display HW.
 */
#ifndef RP_EDID_H
#define RP_EDID_H

#if defined(_KERNEL_MODE)
typedef UCHAR rp_edid_u8;
typedef ULONG rp_edid_u32;
typedef ULONGLONG rp_edid_u64;
#else
#include <stdint.h>
typedef uint8_t rp_edid_u8;
typedef uint32_t rp_edid_u32;
typedef uint64_t rp_edid_u64;
#endif

#define RP_EDID_BLOCK_SIZE 128u
#define RP_EDID_MAX_BLOCKS 8u
#define RP_EDID_MAX_BYTES (RP_EDID_BLOCK_SIZE * RP_EDID_MAX_BLOCKS)

typedef struct RP_EDID_TIMING {
    rp_edid_u32 active_width;
    rp_edid_u32 active_height;
    rp_edid_u32 total_width;
    rp_edid_u32 total_height;
    rp_edid_u32 pixel_rate_hz;
    rp_edid_u32 block_index;
    rp_edid_u32 descriptor_offset;
} RP_EDID_TIMING;

static inline int rp_edid_block_valid(const rp_edid_u8 *block)
{
    rp_edid_u32 i;
    rp_edid_u32 sum = 0;
    if (!block) return 0;
    for (i = 0; i < RP_EDID_BLOCK_SIZE; ++i) sum += block[i];
    return (sum & 0xffu) == 0;
}

static inline int rp_edid_valid(const rp_edid_u8 *edid, rp_edid_u32 bytes)
{
    static const rp_edid_u8 header[8] = {0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00};
    rp_edid_u32 i, blocks;
    if (!edid || bytes < RP_EDID_BLOCK_SIZE || bytes > RP_EDID_MAX_BYTES ||
        (bytes % RP_EDID_BLOCK_SIZE) != 0) return 0;
    for (i = 0; i < 8; ++i) if (edid[i] != header[i]) return 0;
    blocks = bytes / RP_EDID_BLOCK_SIZE;
    if ((rp_edid_u32)edid[126] + 1u != blocks) return 0;
    for (i = 0; i < blocks; ++i)
        if (!rp_edid_block_valid(edid + i * RP_EDID_BLOCK_SIZE)) return 0;
    return 1;
}

static inline int rp_edid_parse_dtd(const rp_edid_u8 *dtd, RP_EDID_TIMING *out)
{
    rp_edid_u32 clock10khz, hactive, hblank, vactive, vblank;
    rp_edid_u64 pixel;
    if (!dtd || !out) return 0;
    clock10khz = (rp_edid_u32)dtd[0] | ((rp_edid_u32)dtd[1] << 8);
    if (!clock10khz || (dtd[17] & 0x80u)) return 0; /* no descriptor / interlaced */
    hactive = (rp_edid_u32)dtd[2] | (((rp_edid_u32)dtd[4] & 0xf0u) << 4);
    hblank  = (rp_edid_u32)dtd[3] | (((rp_edid_u32)dtd[4] & 0x0fu) << 8);
    vactive = (rp_edid_u32)dtd[5] | (((rp_edid_u32)dtd[7] & 0xf0u) << 4);
    vblank  = (rp_edid_u32)dtd[6] | (((rp_edid_u32)dtd[7] & 0x0fu) << 8);
    if (!hactive || !vactive || !hblank || !vblank ||
        hactive > 8192u || vactive > 8192u ||
        hactive + hblank > 16384u || vactive + vblank > 16384u) return 0;
    pixel = (rp_edid_u64)clock10khz * 10000u;
    if (!pixel || pixel > 0xffffffffu) return 0;
    out->active_width = hactive;
    out->active_height = vactive;
    out->total_width = hactive + hblank;
    out->total_height = vactive + vblank;
    out->pixel_rate_hz = (rp_edid_u32)pixel;
    return 1;
}

static inline int rp_edid_match_timing(const rp_edid_u8 *edid, rp_edid_u32 bytes,
                                       rp_edid_u32 width, rp_edid_u32 height,
                                       RP_EDID_TIMING *out)
{
    static const rp_edid_u32 base_offsets[4] = {54u, 72u, 90u, 108u};
    rp_edid_u32 i, block, blocks, offset;
    RP_EDID_TIMING t;
    if (!out || !width || !height || !rp_edid_valid(edid, bytes)) return 0;

    /* Base-block DTDs are checked first; the first DTD is normally preferred. */
    for (i = 0; i < 4; ++i) {
        t.block_index = 0;
        t.descriptor_offset = base_offsets[i];
        if (rp_edid_parse_dtd(edid + base_offsets[i], &t) &&
            t.active_width == width && t.active_height == height) {
            *out = t;
            return 1;
        }
    }

    blocks = bytes / RP_EDID_BLOCK_SIZE;
    for (block = 1; block < blocks; ++block) {
        const rp_edid_u8 *ext = edid + block * RP_EDID_BLOCK_SIZE;
        if (ext[0] != 0x02u) continue; /* CTA-861 extension */
        offset = ext[2];
        if (offset < 4u || offset > 126u) continue;
        while (offset + 18u <= 127u) {
            t.block_index = block;
            t.descriptor_offset = offset;
            if (rp_edid_parse_dtd(ext + offset, &t) &&
                t.active_width == width && t.active_height == height) {
                *out = t;
                return 1;
            }
            offset += 18u;
        }
    }
    return 0;
}

#endif
