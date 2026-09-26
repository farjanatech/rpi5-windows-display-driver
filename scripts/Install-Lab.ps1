# SPDX-License-Identifier: GPL-3.0-only
# Read-only preflight by default. Explicit -Install AND confirmation are required.
[CmdletBinding(SupportsShouldProcess=$true,ConfirmImpact='High')]
param(
    [Parameter(Mandatory)][string]$ArtifactRoot,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{40}$')][string]$ExpectedCommit,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{40}$')][string]$ExpectedThumbprint,
    [Parameter(Mandatory)][string]$DeviceInstanceId,
    [switch]$Install,
    [switch]$RecoveryConfirmed,
    [switch]$DiagnosticsConfirmed,
    [switch]$PowerPolicyConfirmed,
    [switch]$AcknowledgeUntestedHardware
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Lab-Common.ps1')
function Read-DeviceBinding {
    # Enumerate successfully first; an unbound device can legitimately lack an INF/service
    # property. Access/enumeration errors still stop the operation instead of being hidden.
    $properties = @(Get-PnpDeviceProperty -InstanceId $DeviceInstanceId -ErrorAction Stop)
    $binding = @{Inf=$null; Service=$null; Problem=$null}
    foreach ($property in $properties) {
        switch ($property.KeyName) {
            'DEVPKEY_Device_DriverInfPath' { $binding.Inf=$property.Data }
            'DEVPKEY_Device_Service' { $binding.Service=$property.Data }
            'DEVPKEY_Device_ProblemCode' { $binding.Problem=$property.Data }
        }
    }
    return $binding
}
$manifest = Test-LabManifest -ArtifactRoot $ArtifactRoot -ExpectedCommit $ExpectedCommit -ExpectedThumbprint $ExpectedThumbprint
$device = Assert-LabTarget -DeviceInstanceId $DeviceInstanceId
Initialize-LabNative
$ci = [Rpi5Lab.Native]::CodeIntegrity()
if (($ci -band 2) -eq 0) { throw 'The currently booted Windows kernel does not allow test signing. Follow the lab runbook; this script does not change security settings.' }
$previous = Read-DeviceBinding
$previousInf = $previous.Inf
Write-Host "Target: $($device.InstanceId); previous INF: $previousInf; Code Integrity options: $ci"
if (!$previousInf) { Write-Warning 'The target is currently unbound. Verify its boot-display association and recovery route before installation.' }
Write-Warning 'Experimental display-only code. Physical sleep/resume, native mode setting and acceleration are not supported or hardware-validated.'
if (!$Install) {
    Write-Host 'PREFLIGHT ONLY: no driver, trust, registry, power or boot settings changed.'
    return
}
Assert-LabAdministrator
if (!$RecoveryConfirmed -or !$DiagnosticsConfirmed -or !$PowerPolicyConfirmed -or !$AcknowledgeUntestedHardware) {
    throw 'Confirm the restored-image/recovery route, independent diagnostics, disabled sleep/hibernate policy and untested-hardware risk explicitly. See docs/LAB_INSTALL.md.'
}
$hibernate = Get-ItemPropertyValue 'HKLM:\SYSTEM\CurrentControlSet\Control\Power' -Name HibernateEnabled -ErrorAction Stop
if ($hibernate -ne 0) { throw 'Hibernation must already be disabled. This script does not change power policy.' }
if (!$PSCmdlet.ShouldProcess($DeviceInstanceId, 'Trust the pinned LAB certificate, enable this device and install the authenticated experimental package')) { return }
$session = Join-Path (Join-Path $env:ProgramData 'Rpi5Display/Lab') ([guid]::NewGuid().ToString())
New-Item $session -ItemType Directory -Force | Out-Null
$acl = Get-Acl $session
$acl.SetAccessRuleProtection($true,$false)
foreach ($sid in @('S-1-5-18','S-1-5-32-544')) {
    $rule = [Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),
        'FullControl','ContainerInherit,ObjectInherit','None','Allow')
    $acl.AddAccessRule($rule)
}
Set-Acl $session $acl
Copy-Item (Join-Path $ArtifactRoot 'package') $session -Recurse
Copy-Item (Join-Path $ArtifactRoot 'manifest.json'),(Join-Path $ArtifactRoot 'manifest.p7s') $session
$null = Test-LabManifest -ArtifactRoot $session -ExpectedCommit $ExpectedCommit -ExpectedThumbprint $ExpectedThumbprint
$statePath = Join-Path $session 'install-state.json'
$state = [ordered]@{DeviceInstanceId=$DeviceInstanceId; PreviousInf=$previousInf; Commit=$ExpectedCommit;
    Thumbprint=$ExpectedThumbprint; NewlyTrustedStores=@(); PreviousLabEnable=$null; Phase='Prepared';
    PnpExitCode=$null; CurrentInf=$null; CurrentService=$null; ProblemCode=$null; LabGateEnabled=$false; HardwareValidated=$false}
function Save-State { $state | ConvertTo-Json -Depth 6 | Set-Content $statePath -Encoding utf8 }
function Disable-LabGate {
    $key=[Rpi5Lab.Native]::OpenParameters($DeviceInstanceId,$true)
    try { $key.SetValue('LabEnable',0,[Microsoft.Win32.RegistryValueKind]::DWord) } finally { $key.Dispose() }
    $state.LabGateEnabled=$false
    Save-State
}
$gateChanged = $false; $installationAttempted = $false
Save-State
try {
    if ($previousInf -match '^oem[0-9]+\.inf$') {
        $backup = Join-Path $session 'previous-driver'
        New-Item $backup -ItemType Directory | Out-Null
        & pnputil.exe /export-driver $previousInf $backup
        if ($LASTEXITCODE -ne 0) { throw 'Could not export the previous third-party package. Installation stopped.' }
    }
    foreach ($store in @('Root','TrustedPublisher')) {
        if (!(Test-Path "Cert:\LocalMachine\$store\$ExpectedThumbprint")) {
            Import-Certificate -FilePath (Join-Path $session 'package/Rpi5Display.cer') -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
            $state.NewlyTrustedStores += $store; Save-State
        }
    }
    foreach ($name in @('Rpi5Display.sys','Rpi5Display.cat')) {
        $sig = Get-AuthenticodeSignature -LiteralPath (Join-Path $session "package/$name")
        if ($sig.Status -ne 'Valid' -or !$sig.SignerCertificate -or $sig.SignerCertificate.Thumbprint -ine $ExpectedThumbprint) {
            throw "Authenticode verification failed for $name"
        }
    }
    $key = [Rpi5Lab.Native]::OpenParameters($DeviceInstanceId,$true)
    try {
        $state.PreviousLabEnable = $key.GetValue('LabEnable',$null)
        if ($null -ne $state.PreviousLabEnable -and $key.GetValueKind('LabEnable') -ne 'DWord') { throw 'Unexpected existing LabEnable value type.' }
        Save-State
        $key.SetValue('LabEnable',1,[Microsoft.Win32.RegistryValueKind]::DWord)
        $gateChanged = $true; $state.LabGateEnabled=$true
    } finally { $key.Dispose() }
    $state.Phase='Installation attempted'; Save-State
    $installationAttempted=$true
    & pnputil.exe /add-driver (Join-Path $session 'package/Rpi5Display.inf') /install
    $state.PnpExitCode=$LASTEXITCODE; Save-State
    if ($state.PnpExitCode -notin @(0,3010)) { throw "PnPUtil returned $($state.PnpExitCode). Follow the exact-package rollback runbook." }
    $current = Read-DeviceBinding
    $state.CurrentInf=$current.Inf; $state.CurrentService=$current.Service; $state.ProblemCode=$current.Problem
    $state.Phase='Package staged; binding/hardware must be checked'; Save-State
    if ($state.CurrentService -ine 'Rpi5Display') {
        # No latent opt-in if Windows retained another driver. A future test must opt in again.
        Disable-LabGate
        $state.Phase='Staged only; Windows retained another driver; lab gate cleared'; Save-State
        Write-Warning 'Windows did not select this package. The lab gate has been cleared. Driver ranking is not bypassed and no successful deployment is claimed.'
    } else {
        Write-Host "Windows selected $($state.CurrentInf); PnP problem code=$($state.ProblemCode). Confirm the loaded module and presentation counters through the debugger."
        if ($null -ne $state.ProblemCode -and $state.ProblemCode -ne 0 -and $state.PnpExitCode -ne 3010) {
            throw 'The selected device reports a problem. Startup is not a success; use the recorded diagnostics and rollback procedure.'
        }
    }
    Write-Host "Saved rollback state: $statePath"
    Write-Host 'No automatic reboot was requested. Keep recovery available; installation is not a hardware validation result.'
} catch {
    $state.Phase="Failed: $($_.Exception.Message)"; Save-State
    # Do not automatically unload an active display driver. Preserve evidence and stop future starts.
    if ($gateChanged) {
        try { Disable-LabGate } catch { Write-Warning 'Could not clear the lab gate. Use the recovery runbook.' }
    }
    if (!$installationAttempted) {
        foreach ($store in $state.NewlyTrustedStores) { Remove-Item "Cert:\LocalMachine\$store\$ExpectedThumbprint" -ErrorAction SilentlyContinue }
    }
    Write-Warning "Installation did not complete. State and authenticated package retained at $session."
    throw
}
