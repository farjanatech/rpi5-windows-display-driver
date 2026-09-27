/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/* WDK 26100's ARM64 register macros use legacy token concatenation. Suppress
 * C5103 only inside vendor headers; keep /W4 /WX on our implementation. */
#pragma warning(push)
#pragma warning(disable:5103)
#include <ntddk.h>
#include <dispmprt.h>
#pragma warning(pop)
#include "../core/framebuffer.h"
#include "../core/display_handoff.h"
#include "../core/acpi_handoff.h"
#include "../core/vsync_timing.h"
#include "../core/registration_contract.h"
/* Pin the existing, accepted KMDOD ABI; the fix is not a WDDM upgrade. */
C_ASSERT(DXGKDDI_INTERFACE_VERSION == DXGKDDI_INTERFACE_VERSION_WIN8);
C_ASSERT(sizeof(PVOID) == 8);
C_ASSERT(FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiDispatchIoRequest) == 0x28);
#define RP_POOL_TAG '5DpR'
#define RP_MAX_RECTS 4096u
#define RP_DRIVER_VERSION "0.1.11-hzfix-anchor"
VOID RpTraceInitialize(VOID);
VOID RpTraceShutdown(VOID);
VOID RpLog(_In_z_ _Printf_format_string_ PCSTR Format, ...);
#define RP_LOG(...) RpLog(__VA_ARGS__)
typedef enum RP_HANDOFF_SOURCE {
    RpHandoffNone = 0,
    RpHandoffUefiVariable = 1,
    RpHandoffAcpiR5dh = 2
} RP_HANDOFF_SOURCE;

typedef struct RP_ADAPTER {
    PDEVICE_OBJECT Pdo;
    DXGKRNL_INTERFACE Dxgk;
    DXGK_DISPLAY_INFORMATION Display;
    RP_DISPLAY_HANDOFF FirmwareDisplay;
    BOOLEAN FirmwareTimingValid;
    BOOLEAN FirmwareEdidValid;
    ULONG FirmwareVariableAttributes;
    RP_HANDOFF_SOURCE FirmwareHandoffSource;
    BOOLEAN VSyncAdvertised;
    BOOLEAN VSyncHardwareReady;
    ULONG PixelValveIndex;
    PVOID PixelValveRegs;
    SIZE_T PixelValveBytes;
    volatile LONG VSyncInterruptEnabled;
    volatile LONG VSyncAnchorReported;
    volatile LONG64 LastVSyncQpc;
    LONG64 QpcFrequency;
    volatile LONG64 VSyncCount;
    volatile LONG64 ScanLineQueries;
    PVOID Framebuffer;
    SIZE_T FramebufferBytes;
    RP_SURFACE Shadow;
    KMUTEX Mutex;
    EX_RUNDOWN_REF Rundown;
    BOOLEAN RundownClosed;
    volatile LONG Active;
    volatile LONG CrashDisplay;
    BOOLEAN Visible, NeedFull;
    DEVICE_POWER_STATE AdapterPower, MonitorPower;
    ULONG64 Presents;
} RP_ADAPTER;
BOOLEAN RpEnter(RP_ADAPTER *a);
VOID RpLeave(RP_ADAPTER *a);
VOID RpFlush(RP_ADAPTER *a, RP_RECT rect);
VOID RpBlank(RP_ADAPTER *a);
NTSTATUS RpHandoffInitialize(VOID);
NTSTATUS RpReadDisplayHandoff(
    RP_DISPLAY_HANDOFF *handoff,
    PULONG variableAttributes,
    RP_HANDOFF_SOURCE *source,
    ULONG width,
    ULONG height);
BOOLEAN RpVSyncRegistrationAvailable(VOID);
NTSTATUS RpVSyncInitialize(RP_ADAPTER *a, PCM_RESOURCE_LIST resources);
VOID RpVSyncShutdown(RP_ADAPTER *a);
DXGKDDI_CONTROLINTERRUPT RpControlInterrupt;
DXGKDDI_GETSCANLINE RpGetScanLine;
BOOLEAN RpPathValid(const D3DKMDT_VIDPN_PRESENT_PATH *path, BOOLEAN pinned);
DXGKDDI_ISSUPPORTEDVIDPN RpIsSupported;
DXGKDDI_ENUMVIDPNCOFUNCMODALITY RpEnumModes;
DXGKDDI_COMMITVIDPN RpCommit;
DXGKDDI_RECOMMENDMONITORMODES RpRecommendMonitor;
DXGKDDI_RECOMMENDFUNCTIONALVIDPN RpRecommendFunctional;
DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH RpUpdatePath;
DXGKDDI_QUERYVIDPNHWCAPABILITY RpQueryVidPnCaps;
DXGKDDI_PRESENTDISPLAYONLY RpPresent;
DXGKDDI_SETVIDPNSOURCEVISIBILITY RpVisibility;
DXGKDDI_SYSTEM_DISPLAY_ENABLE RpSystemEnable;
DXGKDDI_SYSTEM_DISPLAY_WRITE RpSystemWrite;
