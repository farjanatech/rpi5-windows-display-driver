/* SPDX-License-Identifier: GPL-3.0-only */
#include "display.h"
VOID RpFlush(RP_ADAPTER *a, RP_RECT rect)
{
    LONG y;
    if (!a->Framebuffer || !a->Shadow.data || !a->Visible ||
        a->AdapterPower != PowerDeviceD0 || a->MonitorPower != PowerDeviceD0 || a->CrashDisplay) return;
    for (y = rect.top; y < rect.bottom; ++y) {
        SIZE_T offset = (SIZE_T)y * a->Display.Pitch + (SIZE_T)rect.left * 4;
        WRITE_REGISTER_BUFFER_ULONG((PULONG)((PUCHAR)a->Framebuffer + offset),
            (PULONG)(a->Shadow.data + offset), (ULONG)(rect.right - rect.left));
    }
    KeMemoryBarrier();
}
VOID RpBlank(RP_ADAPTER *a)
{
    ULONG x, y;
    if (!a->Framebuffer || a->CrashDisplay) return;
    for (y = 0; y < a->Display.Height; ++y) {
        volatile ULONG *row = (volatile ULONG *)((PUCHAR)a->Framebuffer + (SIZE_T)y * a->Display.Pitch);
        for (x = 0; x < a->Display.Width; ++x) row[x] = 0;
    }
    KeMemoryBarrier();
}
static RP_RECT RpRect(RECT r)
{
    RP_RECT result = { r.left, r.top, r.right, r.bottom }; return result;
}
NTSTATUS APIENTRY RpPresent(CONST HANDLE context, CONST DXGKARG_PRESENT_DISPLAYONLY *p)
{
    RP_ADAPTER *a = (RP_ADAPTER *)context;
    RP_SURFACE src;
    RP_RECT full;
    ULONG i;
    NTSTATUS result = STATUS_SUCCESS;
    ULONGLONG begin = KeQueryInterruptTime();
    if (!p || p->VidPnSourceId != 0 || p->BytesPerPixel != 4 || p->Pitch <= 0 ||
        p->Flags.Rotate || p->NumMoves > RP_MAX_RECTS || p->NumDirtyRects > RP_MAX_RECTS ||
        (p->NumMoves && !p->pMoves) || (p->NumDirtyRects && !p->pDirtyRect) || !p->pSource) {
        RP_LOG("Present rejected invalid parameters\n"); return STATUS_INVALID_PARAMETER;
    }
    if (!RpEnter(a)) return STATUS_DEVICE_NOT_READY;
    src.data = p->pSource; src.width = a->Display.Width; src.height = a->Display.Height; src.pitch = (ULONG)p->Pitch;
    if (!rp_layout(src.width, src.height, src.pitch, &src.size)) { RpLeave(a); return STATUS_INVALID_PARAMETER; }
    full.left = full.top = 0; full.right = (LONG)src.width; full.bottom = (LONG)src.height;
    __try {
        /* Validate the entire batch before touching either shadow or framebuffer. */
        for (i = 0; i < p->NumMoves; ++i) {
            if (!rp_move_valid(&a->Shadow, RpRect(p->pMoves[i].DestRect), p->pMoves[i].SourcePoint.x, p->pMoves[i].SourcePoint.y)) {
                result = STATUS_INVALID_PARAMETER; __leave;
            }
        }
        for (i = 0; i < p->NumDirtyRects; ++i) {
            if (!rp_rect_valid(src.width, src.height, RpRect(p->pDirtyRect[i]))) { result = STATUS_INVALID_PARAMETER; __leave; }
        }
        if (a->NeedFull) {
            rp_copy(&a->Shadow, &src, full); a->NeedFull = FALSE; RpFlush(a, full);
        } else {
            /* Complete all moves before any dirty-rectangle copy, per the Windows DDI. */
            for (i = 0; i < p->NumMoves; ++i)
                rp_move(&a->Shadow, RpRect(p->pMoves[i].DestRect), p->pMoves[i].SourcePoint.x, p->pMoves[i].SourcePoint.y);
            for (i = 0; i < p->NumDirtyRects; ++i) rp_copy(&a->Shadow, &src, RpRect(p->pDirtyRect[i]));
            for (i = 0; i < p->NumMoves; ++i) RpFlush(a, RpRect(p->pMoves[i].DestRect));
            for (i = 0; i < p->NumDirtyRects; ++i) RpFlush(a, RpRect(p->pDirtyRect[i]));
        }
        ++a->Presents;
        if (a->Presents <= 8 || (a->Presents & 255) == 0)
            RP_LOG("Present count=%llu moves=%lu dirty=%lu elapsedUs=%llu visible=%u\n",
                a->Presents, p->NumMoves, p->NumDirtyRects,
                (KeQueryInterruptTime() - begin) / 10, a->Visible);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        a->NeedFull = TRUE; result = GetExceptionCode();
    }
    if (!NT_SUCCESS(result)) {
        a->NeedFull = TRUE; /* The next accepted present must repair shadow state. */
        RP_LOG("Present failed status=0x%08lx moves=%lu dirty=%lu\n", result, p->NumMoves, p->NumDirtyRects);
    }
    RpLeave(a);
    return result;
}
NTSTATUS APIENTRY RpVisibility(CONST HANDLE context, CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *p)
{
    RP_ADAPTER *a = (RP_ADAPTER *)context;
    RP_RECT full;
    if (!p || (p->VidPnSourceId != 0 && p->VidPnSourceId != D3DDDI_ID_ALL)) return STATUS_INVALID_PARAMETER;
    if (!RpEnter(a)) return STATUS_DEVICE_NOT_READY;
    a->Visible = p->Visible;
    full.left = full.top = 0; full.right = (LONG)a->Display.Width; full.bottom = (LONG)a->Display.Height;
    if (a->Visible) RpFlush(a, full); else RpBlank(a);
    RpLeave(a); return STATUS_SUCCESS;
}
