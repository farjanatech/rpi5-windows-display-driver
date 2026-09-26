/* SPDX-License-Identifier: GPL-3.0-only
 * Bugcheck-only path: nonpageable code/data, no allocation, waits or mutexes.
 */
#include "display.h"
NTSTATUS NTAPI RpSystemEnable(PVOID context, D3DDDI_VIDEO_PRESENT_TARGET_ID target,
    PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS flags, UINT *width, UINT *height, D3DDDIFORMAT *format)
{
    RP_ADAPTER *a = context;
    UNREFERENCED_PARAMETER(flags);
    if (!a || !a->Active || !a->Framebuffer || !width || !height || !format ||
        (target != 0 && target != D3DDDI_ID_UNINITIALIZED)) return STATUS_DEVICE_NOT_READY;
    InterlockedExchange(&a->CrashDisplay, 1);
    *width = a->Display.Width; *height = a->Display.Height; *format = a->Display.ColorFormat;
    return STATUS_SUCCESS;
}
VOID NTAPI RpSystemWrite(PVOID context, PVOID source, UINT width, UINT height, UINT stride, UINT x, UINT y)
{
    RP_ADAPTER *a = context;
    UINT row, col;
    if (!a || !source || !a->Framebuffer || !a->Active || !width || !height ||
        width > a->Display.Width || height > a->Display.Height || x > a->Display.Width - width ||
        y > a->Display.Height - height || stride < width * 4u) return;
    for (row = 0; row < height; ++row) {
        PUCHAR s = (PUCHAR)source + (SIZE_T)row * stride;
        volatile ULONG *d = (volatile ULONG *)((PUCHAR)a->Framebuffer + (SIZE_T)(y + row) * a->Display.Pitch) + x;
        for (col = 0; col < width; ++col) {
            d[col] = (ULONG)s[col * 4] | ((ULONG)s[col * 4 + 1] << 8) |
                ((ULONG)s[col * 4 + 2] << 16) | ((ULONG)s[col * 4 + 3] << 24);
        }
    }
    KeMemoryBarrier();
}
