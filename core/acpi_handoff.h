/* SPDX-License-Identifier: GPL-3.0-only
 * ACPI transport for the versioned Raspberry Pi 5 display handoff.
 */
#ifndef RP_ACPI_HANDOFF_H
#define RP_ACPI_HANDOFF_H

#include "display_handoff.h"

#define RP_ACPI_PROVIDER_SIGNATURE 0x41435049u /* Windows firmware provider 'ACPI' */
#define RP_ACPI_HANDOFF_TABLE_ID   0x48443552u /* Windows table ID 'HD5R' -> ACPI signature "R5DH" */
#define RP_ACPI_HANDOFF_REVISION   1u

#pragma pack(push, 1)
typedef struct RP_ACPI_DESCRIPTION_HEADER {
    rp_h_u32 signature;
    rp_h_u32 length;
    rp_h_u8 revision;
    rp_h_u8 checksum;
    rp_h_u8 oem_id[6];
    rp_h_u64 oem_table_id;
    rp_h_u32 oem_revision;
    rp_h_u32 creator_id;
    rp_h_u32 creator_revision;
} RP_ACPI_DESCRIPTION_HEADER;

typedef struct RP_DISPLAY_HANDOFF_ACPI_TABLE {
    RP_ACPI_DESCRIPTION_HEADER header;
    RP_DISPLAY_HANDOFF handoff;
} RP_DISPLAY_HANDOFF_ACPI_TABLE;
#pragma pack(pop)

static inline int rp_acpi_checksum_valid(const void *buffer, rp_h_u32 length)
{
    const rp_h_u8 *bytes = (const rp_h_u8 *)buffer;
    rp_h_u8 sum = 0;
    rp_h_u32 i;
    if (!buffer || !length) return 0;
    for (i = 0; i < length; ++i) sum = (rp_h_u8)(sum + bytes[i]);
    return sum == 0;
}

static inline int rp_acpi_handoff_table_valid(
    const RP_DISPLAY_HANDOFF_ACPI_TABLE *table,
    rp_h_u32 width,
    rp_h_u32 height)
{
    if (!table ||
        table->header.signature != RP_ACPI_HANDOFF_TABLE_ID ||
        table->header.length != sizeof(*table) ||
        table->header.revision != RP_ACPI_HANDOFF_REVISION ||
        !rp_acpi_checksum_valid(table, table->header.length)) {
        return 0;
    }
    return rp_display_handoff_valid(&table->handoff, width, height);
}

#endif
