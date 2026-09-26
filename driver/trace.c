/* SPDX-License-Identifier: GPL-3.0-only
 * Self-describing ETW messages; no pointers, framebuffer contents or user pixels.
 * Never call from the bugcheck path. No file or registry I/O in present callbacks.
 */
#include "display.h"
#include <ntstrsafe.h>
#include <TraceLoggingProvider.h>
TRACELOGGING_DEFINE_PROVIDER(gRpProvider, "farjanatech.Rpi5Display",
    (0x8d553ba8, 0x56d6, 0x47a4, 0xa5, 0xfb, 0xb5, 0x67, 0xec, 0x13, 0x1e, 0xb3));
VOID RpTraceInitialize(VOID)
{
    NTSTATUS status = TraceLoggingRegister(gRpProvider);
    if (!NT_SUCCESS(status))
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "Rpi5Display: ETW registration failed 0x%08lx\n", status);
}
VOID RpTraceShutdown(VOID) { TraceLoggingUnregister(gRpProvider); }
VOID RpLog(_In_z_ _Printf_format_string_ PCSTR Format, ...)
{
    CHAR message[384];
    va_list args;
    NTSTATUS status;
    va_start(args, Format);
    status = RtlStringCbVPrintfA(message, sizeof(message), Format, args);
    va_end(args);
    if (!NT_SUCCESS(status) && status != STATUS_BUFFER_OVERFLOW) return;
    message[sizeof(message) - 1] = 0;
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL, "Rpi5Display: %s", message);
    TraceLoggingWrite(gRpProvider, "DriverDiagnostic",
        TraceLoggingLevel(4), TraceLoggingString(RP_DRIVER_VERSION, "DriverVersion"),
        TraceLoggingString(message, "Message"));
}
