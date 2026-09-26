# SPDX-License-Identifier: GPL-3.0-only
# CMD front end backend. All logs stay local; there is no download-and-execute path.
[CmdletBinding()]
param([ValidateSet('Install','Collect','Preflight','Uninstall','Help')][string]$Action='Preflight',
    [ValidateRange(1,120)][int]$CaptureSeconds=30,[switch]$Elevated)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Lab-Common.ps1')
. (Join-Path $PSScriptRoot 'Diagnostics.ps1')
if ($Action -eq 'Help') {
    Write-Host 'Rpi5Display LAB: Install.cmd, Preflight.cmd, Collect-Logs.cmd, Uninstall.cmd'
    Write-Host 'Installation requires UAC, a recoverable Windows 11 ARM64 test system and explicit confirmation.'
    Write-Host 'All logs are local. No firmware, boot-security, power policy changes or automatic restart.'
    exit 0
}
# Re-launch exactly this local script through UAC; no command arguments are fetched online.
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$isAdmin -and $Action -ne 'Preflight') {
    if ($Elevated) { Write-Error 'Elevation did not succeed.'; exit 5 }
    $ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $args=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,
        '-Action',$Action,'-CaptureSeconds',[string]$CaptureSeconds,'-Elevated')
    try {
        $child=Start-Process -FilePath $ps -Verb RunAs -ArgumentList (@($args | ForEach-Object { ConvertTo-LabArgument $_ }) -join ' ') -Wait -PassThru
        exit $child.ExitCode
    } catch { Write-Error "Administrator permission declined/unavailable: $($_.Exception.Message)"; exit 5 }
}
$directory=$null; $trace=$null; $transcript=$false; $lock=$null; $lockHeld=$false; $code=1
$result=[ordered]@{Action=$Action; StartedUtc=[DateTime]::UtcNow.ToString('o');
    Outcome='Not completed'; ExitCode=1; DeviceInstanceId=$null; PackageCommit=$null; HardwareValidated=$false}
try {
    if ($isAdmin) { $directory=New-LabProtectedDirectory -Category Logs }
    else {
        $directory=Join-Path $env:TEMP ('Rpi5Display-preflight-'+[guid]::NewGuid().ToString('N'))
        Assert-LabNoReparseAncestors $directory
        New-Item -ItemType Directory -Path $directory | Out-Null
    }
    Start-Transcript -Path (Join-Path $directory 'session.log') -Force | Out-Null; $transcript=$true
    $result | ConvertTo-Json | Set-Content (Join-Path $directory 'result.json') -Encoding utf8
    Write-Host "LOG DIRECTORY: $directory"
    Write-Host 'Do not close this window during installation. Nothing will reboot automatically.'
    if ($Action -in @('Install','Uninstall')) {
        $lock=[Threading.Mutex]::new($false,'Global\Rpi5Display-Lab-Change')
        try { $lockHeld=$lock.WaitOne(0) } catch [Threading.AbandonedMutexException] { $lockHeld=$true }
        if (!$lockHeld) { throw 'Another driver install/uninstall operation is in progress.' }
    }
    if ($isAdmin) { Save-PreviousLabEvidence -Directory $directory }
    Save-LabSnapshot -Directory $directory -Stage before
    $devices=@(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'ACPI\BCM2712\*' })
    if ($devices.Count -eq 1) { $result.DeviceInstanceId=$devices[0].InstanceId }
    if ($Action -in @('Install','Preflight','Uninstall')) {
        if ($devices.Count -ne 1) { throw "Expected exactly one present ACPI\BCM2712 device; found $($devices.Count). No driver change was made." }
        $null=Assert-LabTarget -DeviceInstanceId $result.DeviceInstanceId
    }
    if ($Action -in @('Install','Preflight')) {
        $artifactRoot=$PSScriptRoot
        $pinPath=Join-Path $artifactRoot 'Package-Pin.psd1'
        if (!(Test-Path -LiteralPath $pinPath)) { throw 'Use the extracted CI package: Package-Pin.psd1 is missing. A source checkout is not an installable artifact.' }
        $pin=Import-PowerShellDataFile -LiteralPath $pinPath
        $result.PackageCommit=$pin.Commit
        $manifest=Test-LabManifest -ArtifactRoot $artifactRoot -ExpectedCommit $pin.Commit -ExpectedThumbprint $pin.Thumbprint
        Copy-Item (Join-Path $artifactRoot 'manifest.json') $directory
        $parameters=@{ArtifactRoot=$artifactRoot; ExpectedCommit=$pin.Commit; ExpectedThumbprint=$pin.Thumbprint; DeviceInstanceId=$result.DeviceInstanceId}
        Write-Host "PACKAGE COMMIT: $($pin.Commit)"
        Write-Host "SIGNER: $($pin.Thumbprint)"
        Write-Host 'The pin file is part of this download, not an independent trust source. Verify the ZIP checksum against the trusted GitHub run.'
        & (Join-Path $PSScriptRoot 'Install-Lab.ps1') @parameters
        if ($Action -eq 'Preflight') { $code=0; $result.Outcome='Preflight passed; no driver installed' }
        else {
            Write-Warning 'This is an untested-on-hardware display driver and can black-screen or crash Windows.'
            Write-Host 'Confirm ALL: this ZIP is trusted; a restorable image and independent recovery/debug access work; sleep and hibernation are disabled.'
            if ((Read-Host 'Type INSTALL to confirm these prerequisites and install') -cne 'INSTALL') {
                $code=2; $result.Outcome='Operator declined installation'
            } else {
                $trace=Start-LabTrace -Directory $directory
                if (!$trace.Started) { throw 'Runtime trace could not start. No driver change was attempted; inspect trace-start logs.' }
                $installed=& (Join-Path $PSScriptRoot 'Install-Lab.ps1') @parameters -Install -RecoveryConfirmed -DiagnosticsConfirmed -PowerPolicyConfirmed -AcknowledgeUntestedHardware -Confirm:$false
                $installed | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $directory 'install-result.json') -Encoding utf8
                $result.Outcome=$installed.Phase
                if ($installed.PnpExitCode -eq 3010) { $code=3010 }
                elseif ($installed.StagedInf) { $code=0 }
                else { $code=20 }
                Write-Host "Capturing $CaptureSeconds seconds of driver activity. Move windows and scroll on the physical display."
                Start-Sleep -Seconds $CaptureSeconds
            }
        }
    } elseif ($Action -eq 'Collect') {
        $trace=Start-LabTrace -Directory $directory
        if ($trace.Started) {
            Write-Host "Capturing $CaptureSeconds seconds. Reproduce the display issue now."
            Start-Sleep -Seconds $CaptureSeconds
        } else { Write-Warning 'ETW could not start. Other available evidence will still be collected.' }
        $code=0; $result.Outcome='Diagnostic collection completed; inspect command failures and trace availability'
    } else {
        $props=@(Get-PnpDeviceProperty -InstanceId $result.DeviceInstanceId)
        $service=($props | Where-Object KeyName -eq 'DEVPKEY_Device_Service').Data
        $inf=($props | Where-Object KeyName -eq 'DEVPKEY_Device_DriverInfPath').Data
        if ($service -ine 'Rpi5Display' -or $inf -notmatch '^oem[0-9]+\.inf$') { throw 'Rpi5Display is not the active selected package. No other driver will be removed.' }
        Write-Warning "Removing $inf may interrupt the display. Keep your independent recovery channel open."
        if ((Read-Host 'Type REMOVE to uninstall this exact experimental package') -cne 'REMOVE') { $code=2; $result.Outcome='Operator declined removal' }
        else {
            $trace=Start-LabTrace -Directory $directory
            $removed=& (Join-Path $PSScriptRoot 'Remove-Lab.ps1') -DeviceInstanceId $result.DeviceInstanceId -PublishedInf $inf -Remove -Confirm:$false
            $code=$removed.ExitCode; $result.Outcome='Exact package removal requested; verify fallback driver'
        }
    }
} catch {
    $result.Outcome=$_.Exception.Message; $code=1
    Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
    if ($directory) { $_ | Format-List * -Force | Out-String | Set-Content (Join-Path $directory 'error.txt') }
} finally {
    if ($directory) {
        try { Stop-LabTrace -Trace $trace -Directory $directory } catch { Write-Warning "Trace cleanup error: $_" }
        try { Save-LabSnapshot -Directory $directory -Stage after -DeviceInstanceId $result.DeviceInstanceId } catch { Write-Warning "Final snapshot error: $_" }
        try { Save-LabSystemLogs -Directory $directory } catch { Write-Warning "Windows log collection error: $_" }
        $result.ExitCode=$code; $result['FinishedUtc']=[DateTime]::UtcNow.ToString('o')
        $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $directory 'result.json') -Encoding utf8
        if ($transcript) { Stop-Transcript | Out-Null }
        try { $null=Complete-LabBundle -Directory $directory }
        catch { Write-Warning "ZIP creation failed. Raw logs remain at $directory. $_" }
    }
    if ($lockHeld) { $lock.ReleaseMutex() }
    if ($lock) { $lock.Dispose() }
}
Write-Host "RESULT: $($result.Outcome) (exit $code)"
if ($Elevated) { [void](Read-Host 'Press Enter to close this elevated window') }
exit $code
