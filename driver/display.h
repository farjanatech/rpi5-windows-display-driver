/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <ntddk.h>
#include <dispmprt.h>
#include "../core/framebuffer.h"
#define RP_POOL_TAG '5DpR'
#define RP_MAX_RECTS 4096u
#define RP_LOG(...) DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL, "Rpi5Display: " __VA_ARGS__)
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
