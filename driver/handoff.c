/* SPDX-License-Identifier: GPL-3.0-only
 * UEFI-variable first, ACPI R5DH fallback transport for display timing/EDID.
 */
#include "display.h"
#pragma warning(push)
#pragma warning(disable:5103)
#include <aux_klib.h>
#pragma warning(pop)

static GUID gRpDisplayHandoffGuid =
    { 0x941ce3d8, 0x8c4f, 0x4b9e, { 0xa5, 0x77, 0x1c, 0xc9, 0x82, 0x74, 0x55, 0x31 } };
static NTSTATUS gRpAuxKlibStatus = STATUS_NOT_SUPPORTED;
static BOOLEAN gRpAuxKlibInitialized = FALSE;

NTSTATUS RpHandoffInitialize(VOID)
{
    if (!gRpAuxKlibInitialized) {
        gRpAuxKlibStatus = AuxKlibInitialize();
        gRpAuxKlibInitialized = TRUE;
        RP_LOG("handoff ACPI transport AuxKlibInitialize status=0x%08lx\n",
               gRpAuxKlibStatus);
    }
    return gRpAuxKlibStatus;
}

static NTSTATUS RpReadVariableHandoff(
    RP_DISPLAY_HANDOFF *handoff,
    PULONG attributes,
    ULONG width,
    ULONG height)
{
    RP_DISPLAY_HANDOFF candidate;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"Rpi5DisplayHandoff");
    ULONG bytes = sizeof(candidate);
    ULONG attrs = 0;
    ULONG expectedWidth;
    ULONG expectedHeight;
    NTSTATUS status;

    RtlZeroMemory(&candidate, sizeof(candidate));
    status = ExGetFirmwareEnvironmentVariable(
        &name, &gRpDisplayHandoffGuid, &candidate, &bytes, &attrs);
    if (!NT_SUCCESS(status)) {
        RP_LOG("handoff UEFI variable unavailable status=0x%08lx\n", status);
        return status;
    }

    expectedWidth = width ? width : candidate.timing.hdisplay;
    expectedHeight = height ? height : candidate.timing.vdisplay;
    if (bytes != sizeof(candidate) ||
        !rp_display_handoff_attributes_valid(attrs) ||
        !rp_display_handoff_valid(&candidate, expectedWidth, expectedHeight)) {
        RP_LOG("handoff UEFI variable rejected bytes=%lu signature=0x%08lx version=%u flags=0x%08lx attrs=0x%08lx expected=%lux%lu\n",
               bytes, candidate.signature, candidate.version, candidate.flags,
               attrs, expectedWidth, expectedHeight);
        return STATUS_DATA_ERROR;
    }

    *handoff = candidate;
    if (attributes) *attributes = attrs;
    RP_LOG("handoff transport=uefi-variable display=%lu clockKHz=%lu total=%ux%u edidBlocks=%lu attrs=0x%08lx\n",
           candidate.display_number, candidate.timing.clock_khz,
           candidate.timing.htotal, candidate.timing.vtotal,
           candidate.edid_block_count, attrs);
    return STATUS_SUCCESS;
}

static NTSTATUS RpReadAcpiHandoff(
    RP_DISPLAY_HANDOFF *handoff,
    ULONG width,
    ULONG height)
{
    RP_DISPLAY_HANDOFF_ACPI_TABLE table;
    ULONG returned = 0;
    ULONG expectedWidth;
    ULONG expectedHeight;
    NTSTATUS status;

    if (!NT_SUCCESS(gRpAuxKlibStatus)) {
        RP_LOG("handoff ACPI transport unavailable AuxKlib status=0x%08lx\n",
               gRpAuxKlibStatus);
        return gRpAuxKlibStatus;
    }

    RtlZeroMemory(&table, sizeof(table));
    status = AuxKlibGetSystemFirmwareTable(
        RP_ACPI_PROVIDER_SIGNATURE,
        RP_ACPI_HANDOFF_TABLE_ID,
        &table,
        sizeof(table),
        &returned);
    if (!NT_SUCCESS(status)) {
        RP_LOG("handoff ACPI R5DH unavailable status=0x%08lx returned=%lu expected=%lu\n",
               status, returned, (ULONG)sizeof(table));
        return status;
    }
    if (returned != sizeof(table)) {
        RP_LOG("handoff ACPI R5DH rejected size=%lu expected=%lu\n",
               returned, (ULONG)sizeof(table));
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    expectedWidth = width ? width : table.handoff.timing.hdisplay;
    expectedHeight = height ? height : table.handoff.timing.vdisplay;
    if (!rp_acpi_handoff_table_valid(&table, expectedWidth, expectedHeight)) {
        RP_LOG("handoff ACPI R5DH rejected headerSig=0x%08lx length=%lu revision=%u payloadSig=0x%08lx flags=0x%08lx expected=%lux%lu\n",
               table.header.signature, table.header.length, table.header.revision,
               table.handoff.signature, table.handoff.flags,
               expectedWidth, expectedHeight);
        return STATUS_DATA_ERROR;
    }

    *handoff = table.handoff;
    RP_LOG("handoff transport=acpi-r5dh display=%lu clockKHz=%lu total=%ux%u refreshHint=%u edidBlocks=%lu\n",
           handoff->display_number, handoff->timing.clock_khz,
           handoff->timing.htotal, handoff->timing.vtotal,
           handoff->timing.vrefresh, handoff->edid_block_count);
    return STATUS_SUCCESS;
}

NTSTATUS RpReadDisplayHandoff(
    RP_DISPLAY_HANDOFF *handoff,
    PULONG variableAttributes,
    RP_HANDOFF_SOURCE *source,
    ULONG width,
    ULONG height)
{
    NTSTATUS variableStatus;
    NTSTATUS acpiStatus;

    if (!handoff || !source) return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(handoff, sizeof(*handoff));
    if (variableAttributes) *variableAttributes = 0;
    *source = RpHandoffNone;

    variableStatus = RpReadVariableHandoff(
        handoff, variableAttributes, width, height);
    if (NT_SUCCESS(variableStatus)) {
        *source = RpHandoffUefiVariable;
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(handoff, sizeof(*handoff));
    if (variableAttributes) *variableAttributes = 0;
    acpiStatus = RpReadAcpiHandoff(handoff, width, height);
    if (NT_SUCCESS(acpiStatus)) {
        *source = RpHandoffAcpiR5dh;
        return STATUS_SUCCESS;
    }

    RP_LOG("handoff unavailable both transports variable=0x%08lx acpi=0x%08lx\n",
           variableStatus, acpiStatus);
    return acpiStatus;
}
