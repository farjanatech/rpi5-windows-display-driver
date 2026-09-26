/* SPDX-License-Identifier: GPL-3.0-only
 * Versioned volatile UEFI -> Windows display handoff contract.
 */
#ifndef RP_DISPLAY_HANDOFF_H
#define RP_DISPLAY_HANDOFF_H
#if defined(_KERNEL_MODE)
typedef UCHAR rp_h_u8;
typedef USHORT rp_h_u16;
typedef ULONG rp_h_u32;
typedef ULONGLONG rp_h_u64;
#else
#include <stdint.h>
typedef uint8_t rp_h_u8;
typedef uint16_t rp_h_u16;
typedef uint32_t rp_h_u32;
typedef uint64_t rp_h_u64;
#endif

#define RP_DISPLAY_HANDOFF_SIGNATURE 0x48443552u /* "R5DH" little-endian */
#define RP_DISPLAY_HANDOFF_VERSION 1u
#define RP_DISPLAY_HANDOFF_MAX_EDID_BLOCKS 4u
#define RP_DISPLAY_HANDOFF_TIMING_VALID 0x1u
#define RP_DISPLAY_HANDOFF_EDID_VALID 0x2u
#define RP_DISPLAY_EDID_BLOCK_SIZE 128u
#define RP_DISPLAY_TIMING_FLAG_INTERLACE 0x4u

#pragma pack(push, 1)
typedef struct RP_DISPLAY_TIMING {
    rp_h_u8 display;
    rp_h_u8 padding;
    rp_h_u16 video_id_code;
    rp_h_u32 clock_khz;
    rp_h_u16 hdisplay;
    rp_h_u16 hsync_start;
    rp_h_u16 hsync_end;
    rp_h_u16 htotal;
    rp_h_u16 hskew;
    rp_h_u16 vdisplay;
    rp_h_u16 vsync_start;
    rp_h_u16 vsync_end;
    rp_h_u16 vtotal;
    rp_h_u16 vscan;
    rp_h_u16 vrefresh;
    rp_h_u16 padding2;
    rp_h_u32 flags;
} RP_DISPLAY_TIMING;

typedef struct RP_DISPLAY_HANDOFF {
    rp_h_u32 signature;
    rp_h_u16 version;
    rp_h_u16 size;
    rp_h_u32 display_number;
    rp_h_u32 flags;
    RP_DISPLAY_TIMING timing;
    rp_h_u32 edid_block_count;
    rp_h_u8 edid[RP_DISPLAY_HANDOFF_MAX_EDID_BLOCKS * RP_DISPLAY_EDID_BLOCK_SIZE];
} RP_DISPLAY_HANDOFF;
#pragma pack(pop)

static inline int rp_edid_block_valid(const rp_h_u8 *block, int base)
{
    unsigned i;
    rp_h_u8 sum = 0;
    static const rp_h_u8 header[8] = {0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00};
    if (!block) return 0;
    if (base) {
        for (i = 0; i < 8; ++i) if (block[i] != header[i]) return 0;
    }
    for (i = 0; i < RP_DISPLAY_EDID_BLOCK_SIZE; ++i) sum = (rp_h_u8)(sum + block[i]);
    return sum == 0;
}

static inline int rp_display_timing_valid(const RP_DISPLAY_TIMING *t, rp_h_u32 width, rp_h_u32 height)
{
    if (!t || !t->clock_khz || !width || !height) return 0;
    if (t->hdisplay != width || t->vdisplay != height) return 0;
    if (t->htotal < t->hdisplay || t->vtotal < t->vdisplay) return 0;
    if (t->htotal > 16384u || t->vtotal > 16384u) return 0;
    if (t->hsync_start < t->hdisplay || t->hsync_end < t->hsync_start || t->hsync_end > t->htotal) return 0;
    if (t->vsync_start < t->vdisplay || t->vsync_end < t->vsync_start || t->vsync_end > t->vtotal) return 0;
    /* The current framebuffer-only driver has not validated an interlaced path. */
    if (t->flags & RP_DISPLAY_TIMING_FLAG_INTERLACE) return 0;
    return 1;
}

static inline int rp_display_handoff_valid(const RP_DISPLAY_HANDOFF *h, rp_h_u32 width, rp_h_u32 height)
{
    unsigned i;
    if (!h || h->signature != RP_DISPLAY_HANDOFF_SIGNATURE ||
        h->version != RP_DISPLAY_HANDOFF_VERSION || h->size != sizeof(*h)) return 0;
    if (!(h->flags & RP_DISPLAY_HANDOFF_TIMING_VALID) ||
        !rp_display_timing_valid(&h->timing, width, height)) return 0;
    if (h->flags & RP_DISPLAY_HANDOFF_EDID_VALID) {
        if (!h->edid_block_count || h->edid_block_count > RP_DISPLAY_HANDOFF_MAX_EDID_BLOCKS) return 0;
        for (i = 0; i < h->edid_block_count; ++i)
            if (!rp_edid_block_valid(h->edid + i * RP_DISPLAY_EDID_BLOCK_SIZE, i == 0)) return 0;
    } else if (h->edid_block_count != 0) {
        return 0;
    }
    return 1;
}
#endif
