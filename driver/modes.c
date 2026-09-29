/* SPDX-License-Identifier: GPL-3.0-only
 * Single source/target, identity transform, firmware-selected geometry.
 * This file negotiates Windows VidPN objects; it never changes HDMI timings.
 */
#include "display.h"

BOOLEAN RpPathValid(const D3DKMDT_VIDPN_PRESENT_PATH *p, BOOLEAN pinned)
{
    if (!p || p->VidPnSourceId || p->VidPnTargetId) return FALSE;
    if (p->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY &&
        (pinned || p->ContentTransformation.Scaling != D3DKMDT_VPPS_UNPINNED)) return FALSE;
    if (p->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY &&
        (pinned || p->ContentTransformation.Rotation != D3DKMDT_VPPR_UNPINNED)) return FALSE;
    return p->GammaRamp.Type == D3DDDI_GAMMARAMP_DEFAULT;
}
static VOID RpSignal(RP_ADAPTER *a, D3DKMDT_VIDEO_SIGNAL_INFO *s)
{
    ULONGLONG pixelHz;
    ULONGLONG frameTotal;
    RtlZeroMemory(s, sizeof(*s));
    s->VideoStandard = D3DKMDT_VSS_OTHER;
    s->ActiveSize.cx = a->Display.Width;
    s->ActiveSize.cy = a->Display.Height;
    s->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

    if (a->VSyncAdvertised && a->FirmwareTimingValid && a->VSyncHardwareReady) {
        pixelHz = (ULONGLONG)a->FirmwareDisplay.timing.clock_khz * 1000ULL;
        frameTotal = (ULONGLONG)a->FirmwareDisplay.timing.htotal *
            a->FirmwareDisplay.timing.vtotal;
        s->TotalSize.cx = a->FirmwareDisplay.timing.htotal;
        s->TotalSize.cy = a->FirmwareDisplay.timing.vtotal;
        s->PixelRate = pixelHz;
        s->HSyncFreq.Numerator = (UINT)pixelHz;
        s->HSyncFreq.Denominator = a->FirmwareDisplay.timing.htotal;
        s->VSyncFreq.Numerator = (UINT)pixelHz;
        s->VSyncFreq.Denominator = (UINT)frameTotal;
        return;
    }

    s->TotalSize = s->ActiveSize;
    /* Preserve the 0.1.7 fallback if exp0.7 handoff is absent or rejected. */
    s->VSyncFreq.Numerator = s->VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    s->HSyncFreq.Numerator = s->HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    s->PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
}
static BOOLEAN RpSourceValid(RP_ADAPTER *a, const D3DKMDT_VIDPN_SOURCE_MODE *m)
{
    size_t bytes;
    return m && m->Type == D3DKMDT_RMT_GRAPHICS &&
        m->Format.Graphics.PrimSurfSize.cx == a->Display.Width && m->Format.Graphics.PrimSurfSize.cy == a->Display.Height &&
        m->Format.Graphics.VisibleRegionSize.cx == a->Display.Width && m->Format.Graphics.VisibleRegionSize.cy == a->Display.Height &&
        (m->Format.Graphics.PixelFormat == D3DDDIFMT_A8R8G8B8 || m->Format.Graphics.PixelFormat == D3DDDIFMT_X8R8G8B8) &&
        m->Format.Graphics.PixelValueAccessMode == D3DKMDT_PVAM_DIRECT &&
        (m->Format.Graphics.ColorBasis == D3DKMDT_CB_SCRGB || m->Format.Graphics.ColorBasis == D3DKMDT_CB_SRGB) &&
        rp_layout(m->Format.Graphics.PrimSurfSize.cx, m->Format.Graphics.PrimSurfSize.cy,
            m->Format.Graphics.Stride, &bytes);
}
static BOOLEAN RpTargetValid(RP_ADAPTER *a, const D3DKMDT_VIDPN_TARGET_MODE *m)
{
    if (!m || m->VideoSignalInfo.ActiveSize.cx != a->Display.Width ||
        m->VideoSignalInfo.ActiveSize.cy != a->Display.Height ||
        m->VideoSignalInfo.ScanLineOrdering != D3DDDI_VSSLO_PROGRESSIVE) return FALSE;
    if (a->VSyncAdvertised && a->FirmwareTimingValid && a->VSyncHardwareReady) {
        ULONGLONG pixelHz = (ULONGLONG)a->FirmwareDisplay.timing.clock_khz * 1000ULL;
        if (m->VideoSignalInfo.TotalSize.cx != a->FirmwareDisplay.timing.htotal ||
            m->VideoSignalInfo.TotalSize.cy != a->FirmwareDisplay.timing.vtotal ||
            m->VideoSignalInfo.PixelRate != pixelHz) return FALSE;
    }
    return TRUE;
}
/* Replaces only an unpinned, non-pivot mode set. Retains the OS-assigned mode Id. */
static NTSTATUS RpSourceSet(RP_ADAPTER *a, D3DKMDT_HVIDPN v, const DXGK_VIDPN_INTERFACE *vi, BOOLEAN pivot)
{
    D3DKMDT_HVIDPNSOURCEMODESET set;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *si;
    const D3DKMDT_VIDPN_SOURCE_MODE *pinned = NULL;
    D3DKMDT_VIDPN_SOURCE_MODE *m;
    NTSTATUS st;
    BOOLEAN valid;
    st = vi->pfnAcquireSourceModeSet(v, 0, &set, &si);
    if (!NT_SUCCESS(st)) return st;
    st = si->pfnAcquirePinnedModeInfo(set, &pinned);
    if (!NT_SUCCESS(st) && st != STATUS_GRAPHICS_MODE_NOT_PINNED) { vi->pfnReleaseSourceModeSet(v, set); return st; }
    if (pinned) {
        valid = RpSourceValid(a, pinned);
        si->pfnReleaseModeInfo(set, pinned); vi->pfnReleaseSourceModeSet(v, set);
        return valid ? STATUS_SUCCESS : STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET;
    }
    vi->pfnReleaseSourceModeSet(v, set);
    if (pivot) return STATUS_SUCCESS;
    st = vi->pfnCreateNewSourceModeSet(v, 0, &set, &si);
    if (!NT_SUCCESS(st)) return st;
    st = si->pfnCreateNewModeInfo(set, &m);
    if (NT_SUCCESS(st)) {
        m->Type = D3DKMDT_RMT_GRAPHICS;
        RtlZeroMemory(&m->Format, sizeof(m->Format));
        m->Format.Graphics.PrimSurfSize.cx = m->Format.Graphics.VisibleRegionSize.cx = a->Display.Width;
        m->Format.Graphics.PrimSurfSize.cy = m->Format.Graphics.VisibleRegionSize.cy = a->Display.Height;
        m->Format.Graphics.Stride = a->Display.Width * 4;
        m->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        m->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        m->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
        st = si->pfnAddMode(set, m);
        if (!NT_SUCCESS(st)) si->pfnReleaseModeInfo(set, m);
    }
    if (NT_SUCCESS(st)) st = vi->pfnAssignSourceModeSet(v, 0, set);
    if (!NT_SUCCESS(st)) vi->pfnReleaseSourceModeSet(v, set);
    return st;
}
static NTSTATUS RpTargetSet(RP_ADAPTER *a, D3DKMDT_HVIDPN v, const DXGK_VIDPN_INTERFACE *vi, BOOLEAN pivot)
{
    D3DKMDT_HVIDPNTARGETMODESET set;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *ti;
    const D3DKMDT_VIDPN_TARGET_MODE *pinned = NULL;
    D3DKMDT_VIDPN_TARGET_MODE *m;
    NTSTATUS st;
    BOOLEAN valid;
    st = vi->pfnAcquireTargetModeSet(v, 0, &set, &ti);
    if (!NT_SUCCESS(st)) return st;
    st = ti->pfnAcquirePinnedModeInfo(set, &pinned);
    if (!NT_SUCCESS(st) && st != STATUS_GRAPHICS_MODE_NOT_PINNED) { vi->pfnReleaseTargetModeSet(v, set); return st; }
    if (pinned) {
        valid = RpTargetValid(a, pinned);
        ti->pfnReleaseModeInfo(set, pinned); vi->pfnReleaseTargetModeSet(v, set);
        return valid ? STATUS_SUCCESS : STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET;
    }
    vi->pfnReleaseTargetModeSet(v, set);
    if (pivot) return STATUS_SUCCESS;
    st = vi->pfnCreateNewTargetModeSet(v, 0, &set, &ti);
    if (!NT_SUCCESS(st)) return st;
    st = ti->pfnCreateNewModeInfo(set, &m);
    if (NT_SUCCESS(st)) {
        RpSignal(a, &m->VideoSignalInfo); m->Preference = D3DKMDT_MP_PREFERRED;
        st = ti->pfnAddMode(set, m);
        if (!NT_SUCCESS(st)) ti->pfnReleaseModeInfo(set, m);
    }
    if (NT_SUCCESS(st)) st = vi->pfnAssignTargetModeSet(v, 0, set);
    if (!NT_SUCCESS(st)) vi->pfnReleaseTargetModeSet(v, set);
    return st;
}
static NTSTATUS RpTopology(RP_ADAPTER *a, D3DKMDT_HVIDPN v, const DXGK_VIDPN_INTERFACE **vi,
    D3DKMDT_HVIDPNTOPOLOGY *top, const DXGK_VIDPNTOPOLOGY_INTERFACE **ti, SIZE_T *count)
{
    NTSTATUS st = a->Dxgk.DxgkCbQueryVidPnInterface(v, DXGK_VIDPN_INTERFACE_VERSION_V1, vi);
    if (!NT_SUCCESS(st)) return st;
    st = (*vi)->pfnGetTopology(v, top, ti);
    if (!NT_SUCCESS(st)) return st;
    return (*ti)->pfnGetNumPaths(*top, count);
}
NTSTATUS APIENTRY RpIsSupported(CONST HANDLE context, DXGKARG_ISSUPPORTEDVIDPN *p)
{
    RP_ADAPTER *a = (RP_ADAPTER *)context;
    const DXGK_VIDPN_INTERFACE *vi;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *ti;
    const D3DKMDT_VIDPN_PRESENT_PATH *path;
    D3DKMDT_HVIDPNTOPOLOGY top;
    SIZE_T n;
    NTSTATUS st;
    if (!p) return STATUS_INVALID_PARAMETER;
    p->IsVidPnSupported = FALSE;
    if (!p->hDesiredVidPn) { p->IsVidPnSupported = TRUE; return STATUS_SUCCESS; }
    if (!RpEnter(a)) return STATUS_DEVICE_NOT_READY;
    st = RpTopology(a, p->hDesiredVidPn, &vi, &top, &ti, &n);
    if (NT_SUCCESS(st)) {
        if (!n) p->IsVidPnSupported = TRUE;
        else if (n == 1) {
            st = ti->pfnAcquireFirstPathInfo(top, &path);
            if (NT_SUCCESS(st)) {
                p->IsVidPnSupported = RpPathValid(path, FALSE);
                ti->pfnReleasePathInfo(top, path);
                if (p->IsVidPnSupported) {
                    /* pivot=TRUE validates a pin without replacing an unpinned set. */
                    st = RpSourceSet(a, p->hDesiredVidPn, vi, TRUE);
                    if (NT_SUCCESS(st)) st = RpTargetSet(a, p->hDesiredVidPn, vi, TRUE);
                    if (st == STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET ||
                        st == STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET) {
                        p->IsVidPnSupported = FALSE; st = STATUS_SUCCESS;
                    }
                }
            }
        }
    }
    RpLeave(a); return st;
}
NTSTATUS APIENTRY RpEnumModes(CONST HANDLE context, CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *p)
{
    RP_ADAPTER *a = (RP_ADAPTER *)context;
    const DXGK_VIDPN_INTERFACE *vi;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *ti;
    const D3DKMDT_VIDPN_PRESENT_PATH *path;
    D3DKMDT_VIDPN_PRESENT_PATH update;
    D3DKMDT_HVIDPNTOPOLOGY top;
    SIZE_T n;
    NTSTATUS st;
    if (!p) return STATUS_INVALID_PARAMETER;
    if (!RpEnter(a)) return STATUS_DEVICE_NOT_READY;
    st = RpTopology(a, p->hConstrainingVidPn, &vi, &top, &ti, &n);
    if (!NT_SUCCESS(st) || !n) goto done;
    if (n != 1) { st = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY; goto done; }
    st = ti->pfnAcquireFirstPathInfo(top, &path);
    if (!NT_SUCCESS(st)) goto done;
    if (!RpPathValid(path, FALSE)) {
        ti->pfnReleasePathInfo(top, path); st = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY; goto done;
    }
    update = *path;
    ti->pfnReleasePathInfo(top, path);
    st = RpSourceSet(a, p->hConstrainingVidPn, vi, p->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE && p->EnumPivot.VidPnSourceId == 0);
    if (!NT_SUCCESS(st)) goto done;
    st = RpTargetSet(a, p->hConstrainingVidPn, vi, p->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET && p->EnumPivot.VidPnTargetId == 0);
    if (!NT_SUCCESS(st)) goto done;
    if (p->EnumPivotType != D3DKMDT_EPT_SCALING) {
        RtlZeroMemory(&update.ContentTransformation.ScalingSupport, sizeof(update.ContentTransformation.ScalingSupport));
        update.ContentTransformation.ScalingSupport.Identity = 1;
    }
    if (p->EnumPivotType != D3DKMDT_EPT_ROTATION) {
        RtlZeroMemory(&update.ContentTransformation.RotationSupport, sizeof(update.ContentTransformation.RotationSupport));
        update.ContentTransformation.RotationSupport.Identity = 1;
    }
    st = ti->pfnUpdatePathSupportInfo(top, &update);
done:
    RP_LOG("EnumModes result=0x%08lx\n", st);
    RpLeave(a); return st;
}
NTSTATUS APIENTRY RpCommit(CONST HANDLE context, CONST DXGKARG_COMMITVIDPN *p)
{
    RP_ADAPTER *a = (RP_ADAPTER *)context;
    const DXGK_VIDPN_INTERFACE *vi;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *ti;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *si;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *mi;
    const D3DKMDT_VIDPN_PRESENT_PATH *path;
    const D3DKMDT_VIDPN_SOURCE_MODE *src = NULL;
    const D3DKMDT_VIDPN_TARGET_MODE *dst = NULL;
    D3DKMDT_HVIDPNTOPOLOGY top;
    D3DKMDT_HVIDPNSOURCEMODESET ss;
    D3DKMDT_HVIDPNTARGETMODESET ts;
    SIZE_T n;
    NTSTATUS st;
    if (!p || (p->AffectedVidPnSourceId != 0 && p->AffectedVidPnSourceId != D3DDDI_ID_ALL)) return STATUS_INVALID_PARAMETER;
    if (!RpEnter(a)) return STATUS_DEVICE_NOT_READY;
    st = RpTopology(a, p->hFunctionalVidPn, &vi, &top, &ti, &n);
    if (!NT_SUCCESS(st)) goto done;
    if (!n || p->Flags.PathPoweredOff) { a->Visible = FALSE; RpBlank(a); goto done; }
    if (n != 1) { st = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY; goto done; }
    st = ti->pfnAcquireFirstPathInfo(top, &path);
    if (!NT_SUCCESS(st)) goto done;
    if (!RpPathValid(path, TRUE)) st = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    ti->pfnReleasePathInfo(top, path);
    if (!NT_SUCCESS(st)) goto done;
    st = vi->pfnAcquireSourceModeSet(p->hFunctionalVidPn, 0, &ss, &si);
    if (!NT_SUCCESS(st)) goto done;
    st = si->pfnAcquirePinnedModeInfo(ss, &src);
    if (NT_SUCCESS(st) && !RpSourceValid(a, src)) st = STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET;
    if (src) si->pfnReleaseModeInfo(ss, src);
    vi->pfnReleaseSourceModeSet(p->hFunctionalVidPn, ss);
    if (!NT_SUCCESS(st)) goto done;
    st = vi->pfnAcquireTargetModeSet(p->hFunctionalVidPn, 0, &ts, &mi);
    if (!NT_SUCCESS(st)) goto done;
    st = mi->pfnAcquirePinnedModeInfo(ts, &dst);
    if (NT_SUCCESS(st) && !RpTargetValid(a, dst)) st = STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET;
    if (dst) mi->pfnReleaseModeInfo(ts, dst);
    vi->pfnReleaseTargetModeSet(p->hFunctionalVidPn, ts);
    if (NT_SUCCESS(st)) { a->Visible = TRUE; a->NeedFull = TRUE; }
done:
    RP_LOG("CommitVidPn result=0x%08lx visible=%u\n", st, a->Visible);
    RpLeave(a); return st;
}
NTSTATUS APIENTRY RpRecommendMonitor(CONST HANDLE context, CONST DXGKARG_RECOMMENDMONITORMODES *p)
{
    RP_ADAPTER *a = (RP_ADAPTER *)context;
    D3DKMDT_MONITOR_SOURCE_MODE *m;
    NTSTATUS st;
    if (!p || p->VideoPresentTargetId || !p->pMonitorSourceModeSetInterface) return STATUS_INVALID_PARAMETER;
    if (!RpEnter(a)) return STATUS_DEVICE_NOT_READY;
    st = p->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(p->hMonitorSourceModeSet, &m);
    if (NT_SUCCESS(st)) {
        RpSignal(a, &m->VideoSignalInfo);
        m->ColorBasis = D3DKMDT_CB_SRGB;
        m->ColorCoeffDynamicRanges.FirstChannel = 8;
        m->ColorCoeffDynamicRanges.SecondChannel = 8;
        m->ColorCoeffDynamicRanges.ThirdChannel = 8;
        m->ColorCoeffDynamicRanges.FourthChannel = 0;
        m->Origin = D3DKMDT_MCO_DRIVER; m->Preference = D3DKMDT_MP_PREFERRED;
        st = p->pMonitorSourceModeSetInterface->pfnAddMode(p->hMonitorSourceModeSet, m);
        if (!NT_SUCCESS(st)) p->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(p->hMonitorSourceModeSet, m);
        if (st == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET) st = STATUS_SUCCESS;
    }
    RpLeave(a); return st;
}
NTSTATUS APIENTRY RpRecommendFunctional(CONST HANDLE context, CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *p)
{
    UNREFERENCED_PARAMETER(context); UNREFERENCED_PARAMETER(p);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}
NTSTATUS APIENTRY RpUpdatePath(CONST HANDLE context, CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *p)
{
    UNREFERENCED_PARAMETER(context);
    if (!p) return STATUS_INVALID_PARAMETER;
    return RpPathValid(&p->VidPnPresentPathInfo, TRUE) ? STATUS_SUCCESS : STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}
NTSTATUS APIENTRY RpQueryVidPnCaps(CONST HANDLE context, DXGKARG_QUERYVIDPNHWCAPABILITY *p)
{
    UNREFERENCED_PARAMETER(context);
    if (!p || p->SourceId || p->TargetId) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&p->VidPnHWCaps, sizeof(p->VidPnHWCaps));
    return STATUS_SUCCESS;
}
