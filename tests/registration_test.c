/* SPDX-License-Identifier: GPL-3.0-only
 * Portable LOGICAL table test, not an emulation of a WDK layout or Windows.
 * The real driver and this test consume the same callback-binding names.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../core/registration_contract.h"
#define DECLARE_STUB(field, routine) static void routine(void) {}
RP_DOD_CALLBACK_BINDINGS(DECLARE_STUB)
RP_DOD_VSYNC_CALLBACK_BINDINGS(DECLARE_STUB)
#undef DECLARE_STUB
struct TestTable {
#define DECLARE_FIELD(field, routine) void (*field)(void);
RP_DOD_CALLBACK_BINDINGS(DECLARE_FIELD)
RP_DOD_VSYNC_CALLBACK_BINDINGS(DECLARE_FIELD)
#undef DECLARE_FIELD
};
static unsigned missing(const struct TestTable *table)
{
    unsigned n = 0;
#define CHECK_FIELD(field) if (!table->field) ++n;
    RP_DOD_REQUIRED_ENTRY_CALLBACKS(CHECK_FIELD)
#undef CHECK_FIELD
    return n;
}
int main(void)
{
    struct TestTable table;
    unsigned tested = 0;
    memset(&table, 0, sizeof(table));
    assert(missing(&table) == 11);
#define BIND_FIELD(field, routine) table.field = routine;
    RP_DOD_CALLBACK_BINDINGS(BIND_FIELD)
#undef BIND_FIELD
    assert(table.DxgkDdiDispatchIoRequest == RpDispatchIoRequest);
    assert(table.DxgkDdiInterruptRoutine == RpInterrupt);
    assert(table.DxgkDdiDpcRoutine == RpDpc);
    assert(table.DxgkDdiSetPointerPosition == RpPointerPosition);
    assert(table.DxgkDdiSetPointerShape == RpPointerShape);
    assert(table.DxgkDdiGetScanLine == NULL);
    assert(table.DxgkDdiControlInterrupt == NULL);
    assert(missing(&table) == 0);

#define BIND_VSYNC_FIELD(field, routine) table.field = routine;
    RP_DOD_VSYNC_CALLBACK_BINDINGS(BIND_VSYNC_FIELD)
#undef BIND_VSYNC_FIELD
    assert(table.DxgkDdiGetScanLine == RpGetScanLine);
    assert(table.DxgkDdiControlInterrupt == RpControlInterrupt);

    /* Windows requires the optional VSync pair together, never singly. */
    table.DxgkDdiGetScanLine = NULL;
    assert(table.DxgkDdiControlInterrupt != NULL);
    table.DxgkDdiGetScanLine = RpGetScanLine;
    /* Reproduce the actual 0.1.1 omission: only the dispatch pointer is NULL. */
    table.DxgkDdiDispatchIoRequest = NULL;
    assert(missing(&table) == 1);
    table.DxgkDdiDispatchIoRequest = RpDispatchIoRequest;
#define MUTATE_FIELD(field) { void (*saved)(void) = table.field; table.field = NULL; assert(missing(&table) == 1); table.field = saved; ++tested; }
    RP_DOD_REQUIRED_ENTRY_CALLBACKS(MUTATE_FIELD)
#undef MUTATE_FIELD
    assert(missing(&table) == 0 && tested == 11);
    printf("PASS: old missing-dispatch regression, %u required-entry omissions, and optional KMDOD VSync callback pair modeled; logical test only\n", tested);
    return 0;
}
