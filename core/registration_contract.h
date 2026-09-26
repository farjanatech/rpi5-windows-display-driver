/* SPDX-License-Identifier: GPL-3.0-only
 * Callback names, not copied WDK layouts. Actual types come from the WDK.
 * Shared with the portable registration-table regression test.
 */
#ifndef RP_REGISTRATION_CONTRACT_H
#define RP_REGISTRATION_CONTRACT_H

#define RP_DOD_CALLBACK_BINDINGS(X) \
    X(DxgkDdiAddDevice, RpAdd) \
    X(DxgkDdiStartDevice, RpStart) \
    X(DxgkDdiStopDevice, RpStop) \
    X(DxgkDdiRemoveDevice, RpRemove) \
    X(DxgkDdiDispatchIoRequest, RpDispatchIoRequest) \
    X(DxgkDdiInterruptRoutine, RpInterrupt) \
    X(DxgkDdiDpcRoutine, RpDpc) \
    X(DxgkDdiQueryChildRelations, RpChildren) \
    X(DxgkDdiQueryChildStatus, RpChildStatus) \
    X(DxgkDdiQueryDeviceDescriptor, RpDescriptor) \
    X(DxgkDdiSetPowerState, RpPower) \
    X(DxgkDdiResetDevice, RpReset) \
    X(DxgkDdiUnload, RpUnload) \
    X(DxgkDdiQueryAdapterInfo, RpCaps) \
    X(DxgkDdiSetPointerPosition, RpPointerPosition) \
    X(DxgkDdiSetPointerShape, RpPointerShape) \
    X(DxgkDdiIsSupportedVidPn, RpIsSupported) \
    X(DxgkDdiEnumVidPnCofuncModality, RpEnumModes) \
    X(DxgkDdiRecommendFunctionalVidPn, RpRecommendFunctional) \
    X(DxgkDdiRecommendMonitorModes, RpRecommendMonitor) \
    X(DxgkDdiCommitVidPn, RpCommit) \
    X(DxgkDdiUpdateActiveVidPnPresentPath, RpUpdatePath) \
    X(DxgkDdiQueryVidPnHWCapability, RpQueryVidPnCaps) \
    X(DxgkDdiSetVidPnSourceVisibility, RpVisibility) \
    X(DxgkDdiPresentDisplayOnly, RpPresent) \
    X(DxgkDdiStopDeviceAndReleasePostDisplayOwnership, RpReleasePost) \
    X(DxgkDdiSystemDisplayEnable, RpSystemEnable) \
    X(DxgkDdiSystemDisplayWrite, RpSystemWrite)

/* Initial entry requirements; this is not the whole Windows runtime contract. */
#define RP_DOD_REQUIRED_ENTRY_CALLBACKS(X) \
    X(DxgkDdiAddDevice) \
    X(DxgkDdiStartDevice) \
    X(DxgkDdiStopDevice) \
    X(DxgkDdiRemoveDevice) \
    X(DxgkDdiDispatchIoRequest) \
    X(DxgkDdiQueryChildRelations) \
    X(DxgkDdiQueryChildStatus) \
    X(DxgkDdiQueryDeviceDescriptor) \
    X(DxgkDdiSetPowerState) \
    X(DxgkDdiResetDevice) \
    X(DxgkDdiUnload)

#endif
