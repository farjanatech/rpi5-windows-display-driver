/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (c) 2026 farjanatech contributors.
 * Checked framebuffer operations on ordinary RAM, never on MMIO.
 */
#ifndef RP_FRAMEBUFFER_H
#define RP_FRAMEBUFFER_H
#if defined(_KERNEL_MODE)
/* ntddk.h supplies these types. Do not mix the user CRT and kernel CRT headers. */
typedef UCHAR rp_u8;
typedef ULONG rp_u32;
typedef ULONGLONG rp_u64;
typedef LONG rp_i32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t rp_u8;
typedef uint32_t rp_u32;
typedef uint64_t rp_u64;
typedef int32_t rp_i32;
#endif
#if defined(_KERNEL_MODE)
#define RP_MEMCPY(d, s, n)  RtlCopyMemory((d), (s), (n))
#define RP_MEMMOVE(d, s, n) RtlMoveMemory((d), (s), (n))
#else
#include <string.h>
#define RP_MEMCPY(d, s, n)  memcpy((d), (s), (n))
#define RP_MEMMOVE(d, s, n) memmove((d), (s), (n))
#endif
#define RP_MAX_DIMENSION 8192u
#define RP_MAX_FRAME_BYTES (64u * 1024u * 1024u)
typedef struct RP_RECT { rp_i32 left, top, right, bottom; } RP_RECT;
typedef struct RP_SURFACE {
    rp_u8 *data;
    size_t size;
    rp_u32 width, height, pitch;
} RP_SURFACE;
static inline int rp_layout(rp_u32 w, rp_u32 h, rp_u32 pitch, size_t *bytes)
{
    rp_u64 n;
    if (!bytes || !w || !h || w > RP_MAX_DIMENSION || h > RP_MAX_DIMENSION ||
        pitch < w * 4u || (pitch & 3u) || pitch > RP_MAX_DIMENSION * 4u) return 0;
    n = (rp_u64)pitch * h;
    if (!n || n > RP_MAX_FRAME_BYTES) return 0;
    *bytes = (size_t)n;
    return 1;
}
static inline int rp_surface_valid(const RP_SURFACE *s)
{
    size_t bytes;
    return s && s->data && rp_layout(s->width, s->height, s->pitch, &bytes) && s->size >= bytes;
}
static inline int rp_rect_valid(rp_u32 w, rp_u32 h, RP_RECT r)
{
    return r.left >= 0 && r.top >= 0 && r.right >= r.left && r.bottom >= r.top &&
        (rp_u32)r.right <= w && (rp_u32)r.bottom <= h;
}
static inline int rp_move_valid(const RP_SURFACE *s, RP_RECT dst, rp_i32 sx, rp_i32 sy)
{
    if (!rp_surface_valid(s) || !rp_rect_valid(s->width, s->height, dst) || sx < 0 || sy < 0) return 0;
    return (rp_u64)(rp_u32)sx + (rp_u32)(dst.right - dst.left) <= s->width &&
        (rp_u64)(rp_u32)sy + (rp_u32)(dst.bottom - dst.top) <= s->height;
}
static inline int rp_copy(RP_SURFACE *dst, const RP_SURFACE *src, RP_RECT r)
{
    rp_i32 y;
    size_t count;
    if (!rp_surface_valid(dst) || !rp_surface_valid(src) ||
        !rp_rect_valid(dst->width, dst->height, r) || !rp_rect_valid(src->width, src->height, r)) return 0;
    count = (size_t)(r.right - r.left) * 4u;
    for (y = r.top; y < r.bottom; ++y) {
        size_t d = (size_t)(rp_u32)y * dst->pitch + (size_t)(rp_u32)r.left * 4u;
        size_t s = (size_t)(rp_u32)y * src->pitch + (size_t)(rp_u32)r.left * 4u;
        RP_MEMCPY(dst->data + d, src->data + s, count);
    }
    return 1;
}
static inline int rp_move(RP_SURFACE *s, RP_RECT dst, rp_i32 sx, rp_i32 sy)
{
    rp_i32 rows, row;
    size_t count;
    if (!rp_move_valid(s, dst, sx, sy)) return 0;
    rows = dst.bottom - dst.top;
    count = (size_t)(dst.right - dst.left) * 4u;
    for (row = 0; row < rows; ++row) {
        rp_i32 off = dst.top > sy ? rows - 1 - row : row;
        size_t d = (size_t)(rp_u32)(dst.top + off) * s->pitch + (size_t)(rp_u32)dst.left * 4u;
        size_t a = (size_t)(rp_u32)(sy + off) * s->pitch + (size_t)(rp_u32)sx * 4u;
        RP_MEMMOVE(s->data + d, s->data + a, count);
    }
    return 1;
}
#endif
