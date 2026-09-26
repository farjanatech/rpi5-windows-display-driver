/* SPDX-License-Identifier: GPL-3.0-only
 * Required Windows registration entry for legacy video requests.
 * This KMDOD supports no private or legacy video IOCTLs. It does not inspect
 * request payloads, map addresses, create a user-accessible device, or report
 * an unsupported request as successful.
 */
#include "display.h"
DXGKDDI_DISPATCH_IO_REQUEST RpDispatchIoRequest;
#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, RpDispatchIoRequest)
#endif

NTSTATUS NTAPI RpDispatchIoRequest(PVOID context, ULONG source, PVIDEO_REQUEST_PACKET request)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(source);
    UNREFERENCED_PARAMETER(request);
    /* The DDI contract communicates failure through NTSTATUS. No buffer access
       or output is performed for a request we do not implement. */
    return STATUS_NOT_SUPPORTED;
}
