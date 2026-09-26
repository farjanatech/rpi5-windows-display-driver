/* SPDX-License-Identifier: GPL-3.0-only
 * Original Windows KMDOD implementation against documented WDK interfaces.
 * Experimental: no native HDMI/HVS/V3D programming and no sleep/resume claim.
 */
#include "display.h"
DRIVER_INITIALIZE DriverEntry;
DXGKDDI_ADD_DEVICE RpAdd;
DXGKDDI_START_DEVICE RpStart;
DXGKDDI_STOP_DEVICE RpStop;
DXGKDDI_REMOVE_DEVICE RpRemove;
DXGKDDI_QUERY_CHILD_RELATIONS RpChildren;
DXGKDDI_QUERY_CHILD_STATUS RpChildStatus;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR RpDescriptor;
DXGKDDI_SET_POWER_STATE RpPower;
DXGKDDI_RESET_DEVICE RpReset;
DXGKDDI_UNLOAD RpUnload;
DXGKDDI_QUERYADAPTERINFO RpCaps;
DXGKDDI_PRESENTDISPLAYONLY RpPresent;
DXGKDDI_SETVIDPNSOURCEVISIBILITY RpVisibility;
DXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP RpReleasePost;
DXGKDDI_SYSTEM_DISPLAY_ENABLE RpSystemEnable;
DXGKDDI_SYSTEM_DISPLAY_WRITE RpSystemWrite;

BOOLEAN RpEnter(RP_ADAPTER *a)
{
    if (!a || !ExAcquireRundownProtection(&a->Rundown)) return FALSE;
    if (!InterlockedCompareExchange(&a->Active, 0, 0)) {
        ExReleaseRundownProtection(&a->Rundown); return FALSE;
    }
    KeWaitForSingleObject(&a->Mutex, Executive, KernelMode, FALSE, NULL);
    return TRUE;
}
VOID RpLeave(RP_ADAPTER *a)
{
    KeReleaseMutex(&a->Mutex, FALSE);
    ExReleaseRundownProtection(&a->Rundown);
}
static BOOLEAN RpLabEnabled(PDEVICE_OBJECT pdo)
{
    HANDLE key;
    NTSTATUS st;
    ULONG actual;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"LabEnable");
    struct { KEY_VALUE_PARTIAL_INFORMATION Info; UCHAR Extra[sizeof(ULONG)]; } value;
    st = IoOpenDeviceRegistryKey(pdo, PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key);
    if (!NT_SUCCESS(st)) return FALSE;
    RtlZeroMemory(&value, sizeof(value));
    st = ZwQueryValueKey(key, &name, KeyValuePartialInformation, &value, sizeof(value), &actual);
    ZwClose(key);
    return NT_SUCCESS(st) && value.Info.Type == REG_DWORD && value.Info.DataLength == sizeof(ULONG) &&
        *(UNALIGNED ULONG *)value.Info.Data == 1;
}
static BOOLEAN RpHardwareMatch(PDEVICE_OBJECT pdo)
{
    WCHAR ids[1024];
    ULONG bytes = 0;
    NTSTATUS st;
    SIZE_T off = 0, chars;
    UNICODE_STRING wanted = RTL_CONSTANT_STRING(L"ACPI\\BCM2712");
    st = IoGetDeviceProperty(pdo, DevicePropertyHardwareID, sizeof(ids), ids, &bytes);
    if (!NT_SUCCESS(st) || bytes > sizeof(ids) || bytes < sizeof(WCHAR) || (bytes & 1)) return FALSE;
    chars = bytes / sizeof(WCHAR);
    while (off < chars && ids[off]) {
        SIZE_T end = off;
        UNICODE_STRING item;
        while (end < chars && ids[end]) ++end;
        if (end == chars) return FALSE;
        item.Buffer = &ids[off];
        item.Length = (USHORT)((end - off) * sizeof(WCHAR));
        item.MaximumLength = item.Length;
        if (RtlEqualUnicodeString(&item, &wanted, TRUE)) return TRUE;
        off = end + 1;
    }
    return FALSE;
}
NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT object, PUNICODE_STRING path)
{
    KMDDOD_INITIALIZATION_DATA init;
    RtlZeroMemory(&init, sizeof(init));
    init.Version = DXGKDDI_INTERFACE_VERSION_WIN8;
    init.DxgkDdiAddDevice = RpAdd;
    init.DxgkDdiStartDevice = RpStart;
    init.DxgkDdiStopDevice = RpStop;
    init.DxgkDdiRemoveDevice = RpRemove;
    init.DxgkDdiQueryChildRelations = RpChildren;
    init.DxgkDdiQueryChildStatus = RpChildStatus;
    init.DxgkDdiQueryDeviceDescriptor = RpDescriptor;
    init.DxgkDdiSetPowerState = RpPower;
    init.DxgkDdiResetDevice = RpReset;
    init.DxgkDdiUnload = RpUnload;
    init.DxgkDdiQueryAdapterInfo = RpCaps;
    init.DxgkDdiIsSupportedVidPn = RpIsSupported;
    init.DxgkDdiEnumVidPnCofuncModality = RpEnumModes;
    init.DxgkDdiRecommendFunctionalVidPn = RpRecommendFunctional;
    init.DxgkDdiRecommendMonitorModes = RpRecommendMonitor;
    init.DxgkDdiCommitVidPn = RpCommit;
    init.DxgkDdiUpdateActiveVidPnPresentPath = RpUpdatePath;
    init.DxgkDdiQueryVidPnHWCapability = RpQueryVidPnCaps;
    init.DxgkDdiSetVidPnSourceVisibility = RpVisibility;
    init.DxgkDdiPresentDisplayOnly = RpPresent;
    init.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = RpReleasePost;
    init.DxgkDdiSystemDisplayEnable = RpSystemEnable;
    init.DxgkDdiSystemDisplayWrite = RpSystemWrite;
    /* No interrupts, VSync claims, render DDIs, private interfaces or IOCTLs. */
    RP_LOG("registering experimental firmware-framebuffer KMDOD\n");
    return DxgkInitializeDisplayOnlyDriver(object, path, &init);
}
NTSTATUS NTAPI RpAdd(PDEVICE_OBJECT pdo, PVOID *context)
{
    RP_ADAPTER *a;
    if (!pdo || !context) return STATUS_INVALID_PARAMETER;
    *context = NULL;
    if (!RpHardwareMatch(pdo)) return STATUS_DEVICE_CONFIGURATION_ERROR;
    a = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*a), RP_POOL_TAG);
    if (!a) return STATUS_INSUFFICIENT_RESOURCES;
    a->Pdo = pdo;
    KeInitializeMutex(&a->Mutex, 0);
    ExInitializeRundownProtection(&a->Rundown);
    *context = a;
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpStart(PVOID context, PDXGK_START_INFO start, PDXGKRNL_INTERFACE iface,
                       PULONG sources, PULONG children)
{
    RP_ADAPTER *a = context;
    DXGK_DEVICE_INFO device;
    NTSTATUS st;
    size_t bytes;
    if (!a || !start || !iface || !sources || !children) return STATUS_INVALID_PARAMETER;
    *sources = *children = 0;
    if (!RpLabEnabled(a->Pdo)) {
        RP_LOG("start blocked: per-device LabEnable opt-in is absent\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (a->Active || a->Framebuffer) return STATUS_INVALID_DEVICE_STATE;
    if (iface->Size < FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership) +
        sizeof(iface->DxgkCbAcquirePostDisplayOwnership) || !iface->DxgkCbAcquirePostDisplayOwnership ||
        !iface->DxgkCbGetDeviceInformation || !iface->DxgkCbQueryVidPnInterface) return STATUS_NOT_SUPPORTED;
    RtlZeroMemory(&a->Dxgk, sizeof(a->Dxgk));
    RtlCopyMemory(&a->Dxgk, iface, min(iface->Size, sizeof(a->Dxgk)));
    RtlZeroMemory(&device, sizeof(device));
    st = iface->DxgkCbGetDeviceInformation(iface->DeviceHandle, &device);
    if (!NT_SUCCESS(st)) return st;
    RP_LOG("Windows resources received; translated list present=%u\n", device.TranslatedResourceList != NULL);
    RtlZeroMemory(&a->Display, sizeof(a->Display));
    st = iface->DxgkCbAcquirePostDisplayOwnership(iface->DeviceHandle, &a->Display);
    if (!NT_SUCCESS(st)) { RP_LOG("POST handoff failed: 0x%08lx\n", st); return st; }
    if (!rp_layout(a->Display.Width, a->Display.Height, a->Display.Pitch, &bytes) ||
        a->Display.PhysicAddress.QuadPart <= 0 || (a->Display.PhysicAddress.QuadPart & 3) ||
        (ULONGLONG)a->Display.PhysicAddress.QuadPart > MAXULONGLONG - bytes ||
        (a->Display.ColorFormat != D3DDDIFMT_X8R8G8B8 && a->Display.ColorFormat != D3DDDIFMT_A8R8G8B8)) {
        RP_LOG("invalid POST framebuffer; no address fallback is permitted\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    /* Only the OS-owned POST framebuffer is mapped, never guessed MMIO or user-supplied memory.
       Firmware must keep this region reserved for the entire ownership interval. */
    a->Framebuffer = MmMapIoSpaceEx(a->Display.PhysicAddress, bytes, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!a->Framebuffer) return STATUS_INSUFFICIENT_RESOURCES;
    a->FramebufferBytes = bytes;
    a->Shadow.data = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, RP_POOL_TAG);
    if (!a->Shadow.data) {
        MmUnmapIoSpace(a->Framebuffer, bytes); a->Framebuffer = NULL; a->FramebufferBytes = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    a->Shadow.width = a->Display.Width; a->Shadow.height = a->Display.Height;
    a->Shadow.pitch = a->Display.Pitch; a->Shadow.size = bytes;
    a->Visible = TRUE; a->NeedFull = TRUE;
    a->AdapterPower = a->MonitorPower = PowerDeviceD0;
    a->Presents = 0; a->CrashDisplay = 0;
    if (a->RundownClosed) { ExReInitializeRundownProtection(&a->Rundown); a->RundownClosed = FALSE; }
    InterlockedExchange(&a->Active, 1);
    *sources = *children = 1;
    RP_LOG("started %lux%lu pitch=%lu; no native hardware programming\n", a->Display.Width, a->Display.Height, a->Display.Pitch);
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpStop(PVOID context)
{
    RP_ADAPTER *a = context;
    if (!a) return STATUS_INVALID_PARAMETER;
    InterlockedExchange(&a->Active, 0);
    if (!a->RundownClosed) {
        ExWaitForRundownProtectionRelease(&a->Rundown); a->RundownClosed = TRUE;
    }
    if (a->Framebuffer) { MmUnmapIoSpace(a->Framebuffer, a->FramebufferBytes); a->Framebuffer = NULL; }
    if (a->Shadow.data) { ExFreePoolWithTag(a->Shadow.data, RP_POOL_TAG); a->Shadow.data = NULL; }
    a->FramebufferBytes = 0;
    RP_LOG("stopped after %llu presentations\n", a->Presents);
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpRemove(PVOID context)
{
    if (!context) return STATUS_INVALID_PARAMETER;
    RpStop(context); ExFreePoolWithTag(context, RP_POOL_TAG);
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpReleasePost(PVOID context, D3DDDI_VIDEO_PRESENT_TARGET_ID target, PDXGK_DISPLAY_INFORMATION info)
{
    RP_ADAPTER *a = context;
    if (!a || !info || target != 0) return STATUS_INVALID_PARAMETER;
    *info = a->Display;
    return RpStop(a);
}
NTSTATUS NTAPI RpChildren(PVOID context, PDXGK_CHILD_DESCRIPTOR desc, ULONG size)
{
    UNREFERENCED_PARAMETER(context);
    if (!desc || size < 2 * sizeof(*desc)) return STATUS_BUFFER_TOO_SMALL;
    RtlZeroMemory(desc, size);
    desc[0].ChildDeviceType = TypeVideoOutput;
    desc[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    desc[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_HDMI;
    desc[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    desc[0].ChildUid = 0;
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpChildStatus(PVOID context, PDXGK_CHILD_STATUS status, BOOLEAN nonDestructive)
{
    UNREFERENCED_PARAMETER(context); UNREFERENCED_PARAMETER(nonDestructive);
    if (!status || status->ChildUid != 0) return STATUS_INVALID_PARAMETER;
    if (status->Type != StatusConnection) return STATUS_NOT_SUPPORTED;
    status->HotPlug.Connected = TRUE;
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpDescriptor(PVOID context, ULONG uid, PDXGK_DEVICE_DESCRIPTOR desc)
{
    UNREFERENCED_PARAMETER(context); UNREFERENCED_PARAMETER(desc);
    return uid == 0 ? STATUS_MONITOR_NO_DESCRIPTOR : STATUS_INVALID_PARAMETER;
}
NTSTATUS APIENTRY RpCaps(CONST HANDLE context, CONST DXGKARG_QUERYADAPTERINFO *info)
{
    DXGK_DRIVERCAPS *caps;
    UNREFERENCED_PARAMETER(context);
    if (!info || info->Type != DXGKQAITYPE_DRIVERCAPS) return STATUS_NOT_SUPPORTED;
    if (!info->pOutputData || info->OutputDataSize < sizeof(*caps)) return STATUS_BUFFER_TOO_SMALL;
    caps = info->pOutputData;
    RtlZeroMemory(caps, sizeof(*caps));
    caps->HighestAcceptableAddress.QuadPart = MAXLONGLONG;
    caps->MaxPointerWidth = caps->MaxPointerHeight = 0;
    caps->SupportNonVGA = TRUE;
    /* Software cursor; no GPU acceleration, interrupts, rotation, gamma or power-management claims. */
    return STATUS_SUCCESS;
}
VOID RpFlush(RP_ADAPTER *a, RP_RECT rect)
{
    LONG y;
    if (!a->Visible || a->AdapterPower != PowerDeviceD0 || a->MonitorPower != PowerDeviceD0 || a->CrashDisplay) return;
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
    if (!p || p->VidPnSourceId != 0 || p->BytesPerPixel != 4 || p->Pitch <= 0 ||
        p->Flags.Rotate || p->NumMoves > RP_MAX_RECTS || p->NumDirtyRects > RP_MAX_RECTS ||
        (p->NumMoves && !p->pMoves) || (p->NumDirtyRects && !p->pDirtyRect) || !p->pSource) return STATUS_INVALID_PARAMETER;
    if (!RpEnter(a)) return STATUS_DEVICE_NOT_READY;
    src.data = p->pSource; src.width = a->Display.Width; src.height = a->Display.Height; src.pitch = (ULONG)p->Pitch;
    if (!rp_layout(src.width, src.height, src.pitch, &src.size)) { RpLeave(a); return STATUS_INVALID_PARAMETER; }
    full.left = full.top = 0; full.right = (LONG)src.width; full.bottom = (LONG)src.height;
    __try {
        /* Validate the ENTIRE batch before mutating the shadow or framebuffer. */
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
            for (i = 0; i < p->NumMoves; ++i)
                rp_move(&a->Shadow, RpRect(p->pMoves[i].DestRect), p->pMoves[i].SourcePoint.x, p->pMoves[i].SourcePoint.y);
            for (i = 0; i < p->NumDirtyRects; ++i) rp_copy(&a->Shadow, &src, RpRect(p->pDirtyRect[i]));
            for (i = 0; i < p->NumMoves; ++i) RpFlush(a, RpRect(p->pMoves[i].DestRect));
            for (i = 0; i < p->NumDirtyRects; ++i) RpFlush(a, RpRect(p->pDirtyRect[i]));
        }
        ++a->Presents;
        if (a->Presents == 1 || (a->Presents & 1023) == 0) RP_LOG("present count=%llu\n", a->Presents);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        a->NeedFull = TRUE; result = GetExceptionCode();
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
NTSTATUS NTAPI RpPower(PVOID context, ULONG uid, DEVICE_POWER_STATE power, POWER_ACTION action)
{
    RP_ADAPTER *a = context;
    RP_RECT full;
    if (!a || power < PowerDeviceD0 || power > PowerDeviceD3) return STATUS_INVALID_PARAMETER;
    if (uid != DISPLAY_ADAPTER_HW_ID && uid != 0) return STATUS_INVALID_PARAMETER;
    /* Do not pretend to implement physical adapter suspend. Lab installation requires sleep/hibernation off. */
    if (uid == DISPLAY_ADAPTER_HW_ID && power != PowerDeviceD0 &&
        action != PowerActionShutdown && action != PowerActionShutdownReset && action != PowerActionShutdownOff)
        return STATUS_NOT_SUPPORTED;
    if (!RpEnter(a)) return STATUS_SUCCESS;
    if (uid == DISPLAY_ADAPTER_HW_ID) a->AdapterPower = power; else a->MonitorPower = power;
    full.left = full.top = 0; full.right = (LONG)a->Display.Width; full.bottom = (LONG)a->Display.Height;
    if (power == PowerDeviceD0) RpFlush(a, full); else RpBlank(a);
    RpLeave(a); return STATUS_SUCCESS;
}
VOID NTAPI RpReset(PVOID context) { UNREFERENCED_PARAMETER(context); }
VOID NTAPI RpUnload(VOID) { }
NTSTATUS NTAPI RpSystemEnable(PVOID context, D3DDDI_VIDEO_PRESENT_TARGET_ID target,
    PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS flags, PULONG width, PULONG height, PD3DDDIFORMAT format)
{
    RP_ADAPTER *a = context;
    UNREFERENCED_PARAMETER(flags);
    if (!a || !a->Active || !a->Framebuffer || !width || !height || !format ||
        (target != 0 && target != D3DDDI_ID_UNINITIALIZED)) return STATUS_DEVICE_NOT_READY;
    InterlockedExchange(&a->CrashDisplay, 1);
    *width = a->Display.Width; *height = a->Display.Height; *format = a->Display.ColorFormat;
    return STATUS_SUCCESS;
}
VOID NTAPI RpSystemWrite(PVOID context, PVOID source, ULONG width, ULONG height, ULONG stride, ULONG x, ULONG y)
{
    RP_ADAPTER *a = context;
    ULONG row, col;
    if (!a || !source || !a->Framebuffer || !a->Active || !width || !height ||
        width > a->Display.Width || height > a->Display.Height || x > a->Display.Width - width ||
        y > a->Display.Height - height || stride < width * 4u) return;
    /* Bugcheck path: nonpageable, no allocation, no mutex and no wait. */
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
