/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (c) 2026 farjanatech contributors.
 * Platform-independent checked 32-bit framebuffer operations.
 * These operate on ordinary RAM, never on device registers.
 */
#ifndef RP_FRAMEBUFFER_H
#define RP_FRAMEBUFFER_H
#include <stddef.h>
#include <stdint.h>
#define RP_MAX_DIMENSION 8192u
#define RP_MAX_FRAME_BYTES (64u * 1024u * 1024u)
typedef struct RP_RECT { int32_t left, top, right, bottom; } RP_RECT;
typedef struct RP_SURFACE {
    uint8_t *data;
    size_t size;
    uint32_t width, height, pitch;
} RP_SURFACE;

static inline int rp_layout(uint32_t w, uint32_t h, uint32_t pitch, size_t *bytes)
{
    uint64_t n;
    if (!bytes || !w || !h || w > RP_MAX_DIMENSION || h > RP_MAX_DIMENSION ||
        pitch < w * 4u || (pitch & 3u) || pitch > RP_MAX_DIMENSION * 4u) return 0;
    n = (uint64_t)pitch * h;
    if (!n || n > RP_MAX_FRAME_BYTES) return 0;
    *bytes = (size_t)n;
    return 1;
}
static inline int rp_surface_valid(const RP_SURFACE *s)
{
    size_t bytes;
    return s && s->data && rp_layout(s->width, s->height, s->pitch, &bytes) && s->size >= bytes;
}
static inline int rp_rect_valid(uint32_t w, uint32_t h, RP_RECT r)
{
    return r.left >= 0 && r.top >= 0 && r.right >= r.left && r.bottom >= r.top &&
        (uint32_t)r.right <= w && (uint32_t)r.bottom <= h;
}
static inline int rp_move_valid(const RP_SURFACE *s, RP_RECT dst, int32_t sx, int32_t sy)
{
    if (!rp_surface_valid(s) || !rp_rect_valid(s->width, s->height, dst) || sx < 0 || sy < 0) return 0;
    return (uint64_t)(uint32_t)sx + (uint32_t)(dst.right - dst.left) <= s->width &&
        (uint64_t)(uint32_t)sy + (uint32_t)(dst.bottom - dst.top) <= s->height;
}
static inline int rp_copy(RP_SURFACE *dst, const RP_SURFACE *src, RP_RECT r)
{
    int32_t y;
    size_t x, count;
    if (!rp_surface_valid(dst) || !rp_surface_valid(src) ||
        !rp_rect_valid(dst->width, dst->height, r) || !rp_rect_valid(src->width, src->height, r)) return 0;
    count = (size_t)(r.right - r.left) * 4u;
    for (y = r.top; y < r.bottom; ++y) {
        size_t d = (size_t)(uint32_t)y * dst->pitch + (size_t)(uint32_t)r.left * 4u;
        size_t s = (size_t)(uint32_t)y * src->pitch + (size_t)(uint32_t)r.left * 4u;
        for (x = 0; x < count; ++x) dst->data[d + x] = src->data[s + x];
    }
    return 1;
}
static inline int rp_move(RP_SURFACE *s, RP_RECT dst, int32_t sx, int32_t sy)
{
    int32_t rows, row;
    size_t count;
    if (!rp_move_valid(s, dst, sx, sy)) return 0;
    rows = dst.bottom - dst.top;
    count = (size_t)(dst.right - dst.left) * 4u;
    for (row = 0; row < rows; ++row) {
        int32_t off = dst.top > sy ? rows - 1 - row : row;
        size_t d = (size_t)(uint32_t)(dst.top + off) * s->pitch + (size_t)(uint32_t)dst.left * 4u;
        size_t a = (size_t)(uint32_t)(sy + off) * s->pitch + (size_t)(uint32_t)sx * 4u;
        size_t x;
        if (d > a) { for (x = count; x != 0; --x) s->data[d + x - 1] = s->data[a + x - 1]; }
        else { for (x = 0; x < count; ++x) s->data[d + x] = s->data[a + x]; }
    }
    return 1;
}
#endif
