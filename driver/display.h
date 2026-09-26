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
#define RP_POOL_TAG '5DpR'
#define RP_MAX_RECTS 4096u
#define RP_DRIVER_VERSION "0.1.1"
VOID RpTraceInitialize(VOID);
VOID RpTraceShutdown(VOID);
VOID RpLog(_In_z_ _Printf_format_string_ PCSTR Format, ...);
#define RP_LOG(...) RpLog(__VA_ARGS__)
typedef struct RP_ADAPTER {
    PDEVICE_OBJECT Pdo;
    DXGKRNL_INTERFACE Dxgk;
    DXGK_DISPLAY_INFORMATION Display;
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
