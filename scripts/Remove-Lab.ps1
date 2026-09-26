# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding(SupportsShouldProcess=$true,ConfirmImpact='High')]
param(
    [Parameter(Mandatory)][string]$DeviceInstanceId,
    [Parameter(Mandatory)][ValidatePattern('^oem[0-9]+\.inf$')][string]$PublishedInf,
    [switch]$Remove
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Lab-Common.ps1')
Assert-LabAdministrator
$null = Assert-LabTarget -DeviceInstanceId $DeviceInstanceId
# DISM metadata verifies package identity; never accept an arbitrary inbox or OEM INF.
$driver = Get-WindowsDriver -Online -Driver $PublishedInf -ErrorAction Stop
if ([IO.Path]::GetFileName($driver.OriginalFileName) -ine 'Rpi5Display.inf' -or $driver.ClassName -ine 'Display') {
    throw 'The selected published INF is not the Rpi5Display display package.'
}
Write-Host "Verified removal candidate: $PublishedInf ($($driver.OriginalFileName))"
if (!$Remove) { Write-Host 'PREFLIGHT ONLY: no changes made.'; return }
if (!$PSCmdlet.ShouldProcess($DeviceInstanceId,"Clear the lab gate and uninstall the exact package $PublishedInf")) { return }
Initialize-LabNative
$key=[Rpi5Lab.Native]::OpenParameters($DeviceInstanceId,$true)
try { $key.SetValue('LabEnable',0,[Microsoft.Win32.RegistryValueKind]::DWord) } finally { $key.Dispose() }
& pnputil.exe /delete-driver $PublishedInf /uninstall
$code=$LASTEXITCODE
if ($code -notin @(0,3010)) { throw "PnPUtil returned $code. No force-delete or reboot was attempted; use the recovery runbook." }
Write-Host 'Removal requested. Verify fallback-driver selection and follow any Windows restart requirement.'
Write-Host 'No automatic reboot or certificate deletion was performed. Remove only the trust entries recorded as newly added in install-state.json after rollback is confirmed.'
