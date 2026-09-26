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
DXGKDDI_DISPATCH_IO_REQUEST RpDispatchIoRequest;
DXGKDDI_INTERRUPT_ROUTINE RpInterrupt;
DXGKDDI_DPC_ROUTINE RpDpc;
DXGKDDI_QUERY_CHILD_RELATIONS RpChildren;
DXGKDDI_QUERY_CHILD_STATUS RpChildStatus;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR RpDescriptor;
DXGKDDI_SET_POWER_STATE RpPower;
DXGKDDI_RESET_DEVICE RpReset;
DXGKDDI_UNLOAD RpUnload;
DXGKDDI_QUERYADAPTERINFO RpCaps;
DXGKDDI_SETPOINTERPOSITION RpPointerPosition;
DXGKDDI_SETPOINTERSHAPE RpPointerShape;
DXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP RpReleasePost;

typedef enum RP_START_STAGE {
    RpStartNone = 0,
    RpStartEntered = 1,
    RpStartInterfaceValidated = 2,
    RpStartDeviceInfo = 3,
    RpStartPostOwnership = 4,
    RpStartPostValidated = 5,
    RpStartFramebufferMapped = 6,
    RpStartShadowAllocated = 7,
    RpStartCompleted = 8
} RP_START_STAGE;

static BOOLEAN gRpVSyncRegistrationEnabled = FALSE;

static VOID RpWriteStartDword(HANDLE key, PCWSTR valueName, ULONG value)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, valueName);
    (VOID)ZwSetValueKey(key, &name, 0, REG_DWORD, &value, sizeof(value));
}

/* Persist tiny, non-sensitive startup breadcrumbs in the selected device key.
   ETW normally starts after boot, so these values let a later support bundle
   identify the exact StartDevice stage that failed. */
static VOID RpRecordStartState(RP_ADAPTER *a, RP_START_STAGE stage, NTSTATUS status,
                               const DXGK_DISPLAY_INFORMATION *display, ULONG mapMode)
{
    HANDLE key;
    if (!a || !a->Pdo ||
        !NT_SUCCESS(IoOpenDeviceRegistryKey(a->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) return;
    RpWriteStartDword(key, L"Rpi5DisplayStartStage", (ULONG)stage);
    RpWriteStartDword(key, L"Rpi5DisplayStartStatus", (ULONG)status);
    RpWriteStartDword(key, L"Rpi5DisplayFramebufferMapMode", mapMode);
    if (display) {
        RpWriteStartDword(key, L"Rpi5DisplayPostWidth", display->Width);
        RpWriteStartDword(key, L"Rpi5DisplayPostHeight", display->Height);
        RpWriteStartDword(key, L"Rpi5DisplayPostPitch", display->Pitch);
        RpWriteStartDword(key, L"Rpi5DisplayPostColorFormat", (ULONG)display->ColorFormat);
        RpWriteStartDword(key, L"Rpi5DisplayPostTargetId", display->TargetId);
        RpWriteStartDword(key, L"Rpi5DisplayPostAcpiId", display->AcpiId);
        RpWriteStartDword(key, L"Rpi5DisplayPostPhysLow", display->PhysicAddress.LowPart);
        RpWriteStartDword(key, L"Rpi5DisplayPostPhysHigh", (ULONG)display->PhysicAddress.HighPart);
    }
    ZwClose(key);
}

BOOLEAN RpEnter(RP_ADAPTER *a)
{
    if (!a || !ExAcquireRundownProtection(&a->Rundown)) return FALSE;
    if (!InterlockedCompareExchange(&a->Active, 0, 0)) {
        ExReleaseRundownProtection(&a->Rundown); return FALSE;
    }
    KeWaitForSingleObject(&a->Mutex, Executive, KernelMode, FALSE, NULL);
    /* Stop may have started while this caller waited for the mutex. */
    if (!InterlockedCompareExchange(&a->Active, 0, 0)) {
        KeReleaseMutex(&a->Mutex, FALSE);
        ExReleaseRundownProtection(&a->Rundown);
        return FALSE;
    }
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
static VOID RpLoadFirmwareDisplay(RP_ADAPTER *a)
{
    static GUID handoffGuid = { 0x941ce3d8, 0x8c4f, 0x4b9e, { 0xa5, 0x77, 0x1c, 0xc9, 0x82, 0x74, 0x55, 0x31 } };
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"Rpi5DisplayHandoff");
    ULONG bytes = sizeof(a->FirmwareDisplay);
    ULONG attributes = 0;
    NTSTATUS st;
    HANDLE key;

    RtlZeroMemory(&a->FirmwareDisplay, sizeof(a->FirmwareDisplay));
    a->FirmwareTimingValid = FALSE;
    a->FirmwareEdidValid = FALSE;
    a->FirmwareVariableAttributes = 0;

    st = ExGetFirmwareEnvironmentVariable(&name, &handoffGuid, &a->FirmwareDisplay, &bytes, &attributes);
    if (NT_SUCCESS(st)) a->FirmwareVariableAttributes = attributes;
    if (!NT_SUCCESS(st)) {
        RP_LOG("firmware display handoff unavailable status=0x%08lx; using unspecified timing fallback\n", st);
    } else if (!rp_display_handoff_attributes_valid(attributes) ||
               bytes != sizeof(a->FirmwareDisplay) ||
               !rp_display_handoff_valid(&a->FirmwareDisplay, a->Display.Width, a->Display.Height)) {
        RP_LOG("firmware display handoff rejected bytes=%lu signature=0x%08lx version=%u flags=0x%08lx attrs=0x%08lx\n",
            bytes, a->FirmwareDisplay.signature, a->FirmwareDisplay.version,
            a->FirmwareDisplay.flags, attributes);
        RtlZeroMemory(&a->FirmwareDisplay, sizeof(a->FirmwareDisplay));
    } else {
        a->FirmwareTimingValid = TRUE;
        a->FirmwareEdidValid = (a->FirmwareDisplay.flags & RP_DISPLAY_HANDOFF_EDID_VALID) != 0;
        RP_LOG("firmware display handoff accepted display=%lu clockKHz=%lu total=%ux%u refreshHint=%u edidBlocks=%lu attrs=0x%08lx\n",
            a->FirmwareDisplay.display_number, a->FirmwareDisplay.timing.clock_khz,
            a->FirmwareDisplay.timing.htotal, a->FirmwareDisplay.timing.vtotal,
            a->FirmwareDisplay.timing.vrefresh, a->FirmwareDisplay.edid_block_count, attributes);
    }

    if (NT_SUCCESS(IoOpenDeviceRegistryKey(a->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) {
        RpWriteStartDword(key, L"Rpi5DisplayFirmwareTimingValid", a->FirmwareTimingValid ? 1 : 0);
        RpWriteStartDword(key, L"Rpi5DisplayFirmwareEdidValid", a->FirmwareEdidValid ? 1 : 0);
        RpWriteStartDword(key, L"Rpi5DisplayFirmwareVariableAttributes", a->FirmwareVariableAttributes);
        RpWriteStartDword(key, L"Rpi5DisplayFirmwareClockKHz",
            a->FirmwareTimingValid ? a->FirmwareDisplay.timing.clock_khz : 0);
        RpWriteStartDword(key, L"Rpi5DisplayFirmwareHTotal",
            a->FirmwareTimingValid ? a->FirmwareDisplay.timing.htotal : 0);
        RpWriteStartDword(key, L"Rpi5DisplayFirmwareVTotal",
            a->FirmwareTimingValid ? a->FirmwareDisplay.timing.vtotal : 0);
        RpWriteStartDword(key, L"Rpi5DisplayFirmwareEdidBlocks",
            a->FirmwareEdidValid ? a->FirmwareDisplay.edid_block_count : 0);
        ZwClose(key);
    }
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT object, PUNICODE_STRING path)
{
    KMDDOD_INITIALIZATION_DATA init;
    NTSTATUS status;
    RTL_OSVERSIONINFOW os;
    ULONG missing = 0;
    RpTraceInitialize();
    RtlZeroMemory(&init, sizeof(init));
    init.Version = DXGKDDI_INTERFACE_VERSION;
    #define RP_BIND_CALLBACK(field, function) init.field = function;
    RP_DOD_CALLBACK_BINDINGS(RP_BIND_CALLBACK)
    #undef RP_BIND_CALLBACK

    /*
     * Windows requires GetScanLine + ControlInterrupt as a pair when a KMDOD
     * reports real signal frequencies. Bind the pair only if the exp0.7
     * runtime handoff is already present and valid at DriverEntry.
     */
    gRpVSyncRegistrationEnabled = RpVSyncRegistrationAvailable();
    if (gRpVSyncRegistrationEnabled) {
        #define RP_BIND_VSYNC_CALLBACK(field, function) init.field = function;
        RP_DOD_VSYNC_CALLBACK_BINDINGS(RP_BIND_VSYNC_CALLBACK)
        #undef RP_BIND_VSYNC_CALLBACK
    }

    /* A required dispatch entry must exist even when every legacy IOCTL is unsupported. */
    #define RP_CHECK_CALLBACK(field) if (!init.field) { ++missing; RP_LOG("registration missing callback=%s\n", #field); }
    RP_DOD_REQUIRED_ENTRY_CALLBACKS(RP_CHECK_CALLBACK)
    #undef RP_CHECK_CALLBACK
    if (missing) {
        RP_LOG("registration aborted locally: missing=%lu\n", missing);
        RpTraceShutdown();
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlZeroMemory(&os, sizeof(os));
    os.dwOSVersionInfoSize = sizeof(os);
    status = RtlGetVersion(&os);
    RP_LOG("registration version=%s compiledDDI=0x%lx requestedDDI=0x%lx initBytes=%lu pointerBytes=%lu dispatchOffset=%lu os=%lu.%lu.%lu osStatus=0x%08lx\n",
        RP_DRIVER_VERSION, (ULONG)DXGKDDI_INTERFACE_VERSION, init.Version,
        (ULONG)sizeof(init), (ULONG)sizeof(PVOID),
        (ULONG)FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiDispatchIoRequest),
        os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, status);
    RP_LOG("registering experimental firmware-framebuffer KMDOD vsyncControl=%u\n",
        gRpVSyncRegistrationEnabled);
    status = DxgkInitializeDisplayOnlyDriver(object, path, &init);
    RP_LOG("DriverEntry version=%s status=0x%08lx\n", RP_DRIVER_VERSION, status);
    if (!NT_SUCCESS(status)) RpTraceShutdown();
    return status;
}
NTSTATUS NTAPI RpAdd(PDEVICE_OBJECT pdo, PVOID *context)
{
    RP_ADAPTER *a;
    RP_LOG("AddDevice entered\n");
    if (!pdo || !context) return STATUS_INVALID_PARAMETER;
    *context = NULL;
    if (!RpHardwareMatch(pdo)) {
        RP_LOG("AddDevice rejected: hardware ID is not ACPI\\BCM2712\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    a = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*a), RP_POOL_TAG);
    if (!a) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(a, sizeof(*a));
    a->Pdo = pdo;
    a->VSyncAdvertised = gRpVSyncRegistrationEnabled;
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
    ULONG mapMode = 0;
    if (!a || !start || !iface || !sources || !children) return STATUS_INVALID_PARAMETER;
    *sources = *children = 0;
    RpRecordStartState(a, RpStartEntered, STATUS_PENDING, NULL, 0);
    RP_LOG("StartDevice begin; interface version=0x%08lx size=%lu\n", iface->Version, iface->Size);
    if (!RpLabEnabled(a->Pdo)) {
        RP_LOG("start blocked: per-device LabEnable opt-in is absent\n");
        RpRecordStartState(a, RpStartEntered, STATUS_DEVICE_CONFIGURATION_ERROR, NULL, 0);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (a->Active || a->Framebuffer || a->PixelValveRegs) {
        RpRecordStartState(a, RpStartEntered, STATUS_INVALID_DEVICE_STATE, NULL, 0);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (iface->Size < FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership) +
        sizeof(iface->DxgkCbAcquirePostDisplayOwnership) || !iface->DxgkCbAcquirePostDisplayOwnership ||
        !iface->DxgkCbGetDeviceInformation || !iface->DxgkCbQueryVidPnInterface) {
        RpRecordStartState(a, RpStartEntered, STATUS_NOT_SUPPORTED, NULL, 0);
        return STATUS_NOT_SUPPORTED;
    }
    RpRecordStartState(a, RpStartInterfaceValidated, STATUS_SUCCESS, NULL, 0);
    RtlZeroMemory(&a->Dxgk, sizeof(a->Dxgk));
    RtlCopyMemory(&a->Dxgk, iface, min(iface->Size, sizeof(a->Dxgk)));
    RtlZeroMemory(&device, sizeof(device));
    st = iface->DxgkCbGetDeviceInformation(iface->DeviceHandle, &device);
    if (!NT_SUCCESS(st)) {
        RP_LOG("GetDeviceInformation failed 0x%08lx\n", st);
        RpRecordStartState(a, RpStartInterfaceValidated, st, NULL, 0);
        return st;
    }
    RpRecordStartState(a, RpStartDeviceInfo, STATUS_SUCCESS, NULL, 0);
    RP_LOG("Windows resources received; translated list present=%u\n", device.TranslatedResourceList != NULL);
    RtlZeroMemory(&a->Display, sizeof(a->Display));
    /* Match Microsoft's KMDOD handoff contract exactly: boot-time TargetId may
       legitimately remain D3DDDI_ID_UNINITIALIZED. */
    a->Display.TargetId = D3DDDI_ID_UNINITIALIZED;
    st = iface->DxgkCbAcquirePostDisplayOwnership(iface->DeviceHandle, &a->Display);
    RpRecordStartState(a, RpStartPostOwnership, st, &a->Display, 0);
    if (!NT_SUCCESS(st)) { RP_LOG("POST handoff failed: 0x%08lx\n", st); return st; }
    if (a->Display.Width == 0) {
        RP_LOG("POST handoff returned no active POST display\n");
        RpRecordStartState(a, RpStartPostOwnership, STATUS_UNSUCCESSFUL, &a->Display, 0);
        return STATUS_UNSUCCESSFUL;
    }
    RP_LOG("POST handoff width=%lu height=%lu pitch=%lu format=%u target=%lu acpi=%lu\n",
        a->Display.Width, a->Display.Height, a->Display.Pitch, (UINT)a->Display.ColorFormat,
        a->Display.TargetId, a->Display.AcpiId);
    if (!rp_layout(a->Display.Width, a->Display.Height, a->Display.Pitch, &bytes) ||
        a->Display.PhysicAddress.QuadPart <= 0 || (a->Display.PhysicAddress.QuadPart & 3) ||
        (ULONGLONG)a->Display.PhysicAddress.QuadPart > MAXULONGLONG - bytes ||
        (a->Display.ColorFormat != D3DDDIFMT_X8R8G8B8 && a->Display.ColorFormat != D3DDDIFMT_A8R8G8B8)) {
        RP_LOG("invalid POST framebuffer; no address fallback is permitted\n");
        RpRecordStartState(a, RpStartPostOwnership, STATUS_DEVICE_CONFIGURATION_ERROR, &a->Display, 0);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    RpRecordStartState(a, RpStartPostValidated, STATUS_SUCCESS, &a->Display, 0);
    /*
     * Timing/EDID remains optional for the legacy fallback registration. If
     * DriverEntry advertised VSync control, however, Windows requires real
     * timing plus working GetScanLine/ControlInterrupt support.
     */
    RpLoadFirmwareDisplay(a);
    if (a->VSyncAdvertised) {
        if (!a->FirmwareTimingValid) {
            RP_LOG("VSync registration was advertised but firmware timing is unavailable\n");
            RpRecordStartState(a, RpStartPostValidated,
                               STATUS_DEVICE_CONFIGURATION_ERROR, &a->Display, 0);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        st = RpVSyncInitialize(a, device.TranslatedResourceList);
        if (!NT_SUCCESS(st)) {
            RP_LOG("VSync hardware initialization failed 0x%08lx\n", st);
            RpRecordStartState(a, RpStartPostValidated, st, &a->Display, 0);
            return st;
        }
    }

    /* Map only the OS-owned POST framebuffer. Match Microsoft's KMDOD sample:
       prefer write-combining, then retry non-cached if the platform rejects WC. */
    a->Framebuffer = MmMapIoSpaceEx(a->Display.PhysicAddress, bytes, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (a->Framebuffer) {
        mapMode = 1;
    } else {
        RP_LOG("Framebuffer WC mapping failed; retrying non-cached bytes=%llu\n", (ULONGLONG)bytes);
        a->Framebuffer = MmMapIoSpaceEx(a->Display.PhysicAddress, bytes, PAGE_READWRITE | PAGE_NOCACHE);
        if (a->Framebuffer) mapMode = 2;
    }
    if (!a->Framebuffer) {
        RP_LOG("Framebuffer mapping failed in both cache modes bytes=%llu\n", (ULONGLONG)bytes);
        RpVSyncShutdown(a);
        RpRecordStartState(a, RpStartPostValidated, STATUS_NO_MEMORY, &a->Display, 0);
        return STATUS_NO_MEMORY;
    }
    a->FramebufferBytes = bytes;
    RpRecordStartState(a, RpStartFramebufferMapped, STATUS_SUCCESS, &a->Display, mapMode);
    a->Shadow.data = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, RP_POOL_TAG);
    if (!a->Shadow.data) {
        RP_LOG("Shadow allocation failed bytes=%llu\n", (ULONGLONG)bytes);
        MmUnmapIoSpace(a->Framebuffer, bytes); a->Framebuffer = NULL; a->FramebufferBytes = 0;
        RpVSyncShutdown(a);
        RpRecordStartState(a, RpStartFramebufferMapped, STATUS_INSUFFICIENT_RESOURCES, &a->Display, mapMode);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RpRecordStartState(a, RpStartShadowAllocated, STATUS_SUCCESS, &a->Display, mapMode);
    a->Shadow.width = a->Display.Width; a->Shadow.height = a->Display.Height;
    a->Shadow.pitch = a->Display.Pitch; a->Shadow.size = bytes;
    a->Visible = TRUE; a->NeedFull = TRUE;
    a->AdapterPower = a->MonitorPower = PowerDeviceD0;
    a->Presents = 0; a->CrashDisplay = 0;
    if (a->RundownClosed) { ExReInitializeRundownProtection(&a->Rundown); a->RundownClosed = FALSE; }
    InterlockedExchange(&a->Active, 1);
    *sources = *children = 1;
    RpRecordStartState(a, RpStartCompleted, STATUS_SUCCESS, &a->Display, mapMode);
    RP_LOG("started %lux%lu pitch=%lu mapMode=%lu vsync=%u pv=%lu; no mode programming\n",
        a->Display.Width, a->Display.Height, a->Display.Pitch, mapMode,
        a->VSyncHardwareReady, a->PixelValveIndex);
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpStop(PVOID context)
{
    RP_ADAPTER *a = context;
    if (!a) return STATUS_INVALID_PARAMETER;
    InterlockedExchange(&a->Active, 0);
    RpVSyncShutdown(a);
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
    RtlZeroMemory(info, sizeof(*info));
    if (!RpEnter(a)) return STATUS_DEVICE_NOT_READY;
    /* Keep the firmware-configured pipeline enabled and return our logical target.
       No native power-down was performed, so do not invent an HDMI reset. */
    a->Visible = TRUE;
    a->AdapterPower = a->MonitorPower = PowerDeviceD0;
    RpBlank(a);
    *info = a->Display;
    info->TargetId = 0;
    RP_LOG("ReleasePost width=%lu height=%lu target=0 acpi=%lu\n",
        info->Width, info->Height, info->AcpiId);
    RpLeave(a);
    return RpStop(a);
}
/* The hardware-backed ISR is implemented in vsync.c. */
VOID NTAPI RpDpc(PVOID context)
{
    RP_ADAPTER *a = context;
    if (a && InterlockedCompareExchange(&a->Active, 0, 0) &&
        a->Dxgk.DxgkCbNotifyDpc && a->Dxgk.DeviceHandle) {
        a->Dxgk.DxgkCbNotifyDpc(a->Dxgk.DeviceHandle);
    }
}

NTSTATUS APIENTRY RpPointerPosition(CONST HANDLE context, CONST DXGKARG_SETPOINTERPOSITION *position)
{
    RP_ADAPTER *a = (RP_ADAPTER *)context;
    if (!a || !position || position->VidPnSourceId != 0) return STATUS_INVALID_PARAMETER;
    /* No hardware cursor is advertised. Windows can still request that an
       existing pointer be hidden during mode transitions. */
    if (!position->Flags.Visible) return STATUS_SUCCESS;
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS APIENTRY RpPointerShape(CONST HANDLE context, CONST DXGKARG_SETPOINTERSHAPE *shape)
{
    UNREFERENCED_PARAMETER(context);
    if (!shape || shape->VidPnSourceId != 0) return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NTAPI RpChildren(PVOID context, PDXGK_CHILD_DESCRIPTOR desc, ULONG size)
{
    RP_ADAPTER *a = context;
    RP_LOG("QueryChildRelations bytes=%lu\n", size);
    if (!a) return STATUS_INVALID_PARAMETER;
    if (!desc || size < 2 * sizeof(*desc)) return STATUS_BUFFER_TOO_SMALL;
    RtlZeroMemory(desc, size);
    desc[0].ChildDeviceType = TypeVideoOutput;
    desc[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    desc[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_HDMI;
    desc[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    desc[0].ChildUid = 0;
    desc[0].AcpiUid = a->Display.AcpiId;
    RP_LOG("QueryChildRelations target=0 acpi=%lu alwaysConnected=1\n", desc[0].AcpiUid);
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpChildStatus(PVOID context, PDXGK_CHILD_STATUS status, BOOLEAN nonDestructive)
{
    UNREFERENCED_PARAMETER(context); UNREFERENCED_PARAMETER(nonDestructive);
    RP_LOG("QueryChildStatus uid=%lu type=%u\n", status ? status->ChildUid : MAXULONG,
        status ? (UINT)status->Type : MAXUINT);
    if (!status || status->ChildUid != 0) return STATUS_INVALID_PARAMETER;
    if (status->Type != StatusConnection) return STATUS_NOT_SUPPORTED;
    status->HotPlug.Connected = TRUE;
    return STATUS_SUCCESS;
}
NTSTATUS NTAPI RpDescriptor(PVOID context, ULONG uid, PDXGK_DEVICE_DESCRIPTOR desc)
{
    RP_ADAPTER *a = context;
    ULONG total, remaining, count;
    if (!a || !desc || uid != 0) return STATUS_INVALID_PARAMETER;
    if (!a->FirmwareEdidValid) {
        RP_LOG("QueryDeviceDescriptor uid=0; no validated firmware EDID\n");
        return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
    }
    total = a->FirmwareDisplay.edid_block_count * RP_DISPLAY_EDID_BLOCK_SIZE;
    if (desc->DescriptorOffset >= total) return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    remaining = total - desc->DescriptorOffset;
    count = min(desc->DescriptorLength, remaining);
    if (!count) return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    RtlCopyMemory(desc->DescriptorBuffer,
        a->FirmwareDisplay.edid + desc->DescriptorOffset, count);
    RP_LOG("QueryDeviceDescriptor offset=%lu requested=%lu returned=%lu total=%lu\n",
        desc->DescriptorOffset, desc->DescriptorLength, count, total);
    return STATUS_SUCCESS;
}
NTSTATUS APIENTRY RpCaps(CONST HANDLE context, CONST DXGKARG_QUERYADAPTERINFO *info)
{
    UNREFERENCED_PARAMETER(context);
    if (!info) return STATUS_INVALID_PARAMETER;
    RP_LOG("QueryAdapterInfo type=%u outputBytes=%lu\n", (UINT)info->Type, info->OutputDataSize);

    switch (info->Type) {
    case DXGKQAITYPE_DRIVERCAPS:
    {
        DXGK_DRIVERCAPS *caps;
        if (!info->pOutputData || info->OutputDataSize < sizeof(*caps)) return STATUS_BUFFER_TOO_SMALL;
        caps = info->pOutputData;
        RtlZeroMemory(caps, sizeof(*caps));
        caps->HighestAcceptableAddress.QuadPart = -1;
        caps->MaxPointerWidth = caps->MaxPointerHeight = 0;
        /* KMDOD is a WDDM 1.2 display-only model. Microsoft's reference KMDOD
           explicitly reports v1.2 here. */
        caps->WDDMVersion = DXGKDDI_WDDMv1_2;
        caps->SupportNonVGA = TRUE;
        /* Optimized screen-rotation capability is mandatory for WDDM 1.2
           display-only drivers. This prototype still advertises only the
           identity rotation in its VidPN path support, so enabling this cap
           does not add or claim non-identity rotation modes. */
        caps->SupportSmoothRotation = TRUE;
        RP_LOG("DriverCaps WDDM=%u NonVGA=%u SmoothRotation=%u HighestAddress=0x%llx\n",
            (UINT)caps->WDDMVersion, caps->SupportNonVGA, caps->SupportSmoothRotation,
            (ULONGLONG)caps->HighestAcceptableAddress.QuadPart);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_64BITONLYCAPS:
    {
        DXGK_64_BIT_ONLY_CAPS *caps64;
        if (!info->pOutputData || info->OutputDataSize < sizeof(*caps64)) return STATUS_BUFFER_TOO_SMALL;
        caps64 = info->pOutputData;
        /* There is no user-mode rendering component in this KMDOD prototype.
           Report no 64-bit-only UMD requirement and keep every reserved bit zero.
           Windows 11 ARM64 queries this capability during adapter start. */
        RtlZeroMemory(caps64, sizeof(*caps64));
        RP_LOG("64BitOnlyCaps SupportsOnly64Bit=0 bytes=%lu\n", info->OutputDataSize);
        return STATUS_SUCCESS;
    }

    default:
        RP_LOG("QueryAdapterInfo unsupported type=%u\n", (UINT)info->Type);
        return STATUS_NOT_SUPPORTED;
    }
}
NTSTATUS NTAPI RpPower(PVOID context, ULONG uid, DEVICE_POWER_STATE power, POWER_ACTION action)
{
    RP_ADAPTER *a = context;
    RP_RECT full;
    RP_LOG("Power uid=%lu state=%u action=%u\n", uid, (UINT)power, (UINT)action);
    if (!a || power < PowerDeviceD0 || power > PowerDeviceD3) return STATUS_INVALID_PARAMETER;
    if (uid != DISPLAY_ADAPTER_HW_ID && uid != 0) return STATUS_INVALID_PARAMETER;
    /* Physical adapter suspend is not implemented. Do not report it as working.
       The lab runbook requires sleep/hibernation disabled before opting in. */
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
VOID NTAPI RpUnload(VOID) { RP_LOG("Unload\n"); RpTraceShutdown(); }
