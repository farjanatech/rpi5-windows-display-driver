/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../core/acpi_handoff.h"

static void finalize_checksum(RP_DISPLAY_HANDOFF_ACPI_TABLE *table)
{
    uint32_t i;
    uint8_t sum = 0;
    uint8_t *bytes = (uint8_t *)table;
    table->header.checksum = 0;
    for (i = 0; i < sizeof(*table); ++i) sum = (uint8_t)(sum + bytes[i]);
    table->header.checksum = (uint8_t)(0u - sum);
}

int main(void)
{
    RP_DISPLAY_HANDOFF_ACPI_TABLE table;
    memset(&table, 0, sizeof(table));
    assert(sizeof(RP_ACPI_DESCRIPTION_HEADER) == 36);

    table.header.signature = RP_ACPI_HANDOFF_TABLE_ID;
    table.header.length = sizeof(table);
    table.header.revision = RP_ACPI_HANDOFF_REVISION;

    table.handoff.signature = RP_DISPLAY_HANDOFF_SIGNATURE;
    table.handoff.version = RP_DISPLAY_HANDOFF_VERSION;
    table.handoff.size = sizeof(table.handoff);
    table.handoff.display_number = 2;
    table.handoff.flags = RP_DISPLAY_HANDOFF_TIMING_VALID;
    table.handoff.timing.display = 2;
    table.handoff.timing.clock_khz = 148500;
    table.handoff.timing.hdisplay = 1920;
    table.handoff.timing.hsync_start = 2008;
    table.handoff.timing.hsync_end = 2052;
    table.handoff.timing.htotal = 2200;
    table.handoff.timing.vdisplay = 1080;
    table.handoff.timing.vsync_start = 1084;
    table.handoff.timing.vsync_end = 1089;
    table.handoff.timing.vtotal = 1125;

    finalize_checksum(&table);
    assert(rp_acpi_handoff_table_valid(&table, 1920, 1080));

    table.header.creator_revision ^= 1u;
    assert(!rp_acpi_handoff_table_valid(&table, 1920, 1080));

    puts("PASS: R5DH ACPI layout, checksum and handoff validation");
    return 0;
}
