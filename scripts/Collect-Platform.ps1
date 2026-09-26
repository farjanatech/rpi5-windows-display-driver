# SPDX-License-Identifier: GPL-3.0-only
# Read-only collection: writes only the requested local report, never uploads it.
[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path $env:TEMP 'Rpi5Display-platform.json'),
    [string]$FirmwareCommit = 'UNRECORDED',
    [string]$BoardRevision = 'UNRECORDED',
    [string]$HdmiPort = 'UNRECORDED'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Collect the baseline on the Windows test Pi.' }
. (Join-Path $PSScriptRoot 'Lab-Common.ps1')
function Read-Optional([scriptblock]$Read) {
    try { & $Read } catch { "UNAVAILABLE: $($_.Exception.Message)" }
}
$os = Get-CimInstance Win32_OperatingSystem
$computer = Get-CimInstance Win32_ComputerSystem
$bios = Get-CimInstance Win32_BIOS
$devices = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'ACPI\BCM2712\*' })
$adapters = @()
foreach ($device in $devices) {
    $entry = [ordered]@{InstanceId=$device.InstanceId; Status=$device.Status; Properties=@{}}
    foreach ($key in @('HardwareIds','CompatibleIds','Service','DriverInfPath','DriverVersion','ProblemCode')) {
        $entry.Properties[$key] = Read-Optional { (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName "DEVPKEY_Device_$key" -ErrorAction Stop).Data }
    }
    $entry['ResourcesAndDriverRanking'] = Read-Optional {
        $text = & pnputil.exe /enum-devices /instanceid $device.InstanceId /resources /drivers 2>&1 | Out-String
        @{ExitCode=$LASTEXITCODE; Output=$text}
    }
    $adapters += $entry
}
$ci = Read-Optional { Initialize-LabNative; [Rpi5Lab.Native]::CodeIntegrity() }
$report = [ordered]@{
    SchemaVersion=1; CapturedUtc=(Get-Date).ToUniversalTime().ToString('o');
    Windows=@{Caption=$os.Caption; Build=$os.BuildNumber; Version=$os.Version; Architecture=$os.OSArchitecture;
        UBR=(Read-Optional { (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').UBR })};
    Platform=@{Manufacturer=$computer.Manufacturer; Model=$computer.Model; RamBytes=$computer.TotalPhysicalMemory;
        BoardRevision=$BoardRevision; HdmiPort=$HdmiPort};
    Firmware=@{ReportedVersion=$bios.SMBIOSBIOSVersion; ReportedReleaseDate=$bios.ReleaseDate; SourceCommit=$FirmwareCommit};
    CodeIntegrityOptions=$ci;
    SecureBoot=(Read-Optional { Confirm-SecureBootUEFI });
    DisplayDevices=$adapters;
    VideoControllers=(Read-Optional { Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentBitsPerPixel,CurrentRefreshRate });
    PowerCapabilities=(Read-Optional { & powercfg.exe /a 2>&1 | Out-String });
    SleepPolicy=(Read-Optional { & powercfg.exe /query SCHEME_CURRENT SUB_SLEEP 2>&1 | Out-String });
    KernelDebugBootConfiguration=(Read-Optional { & bcdedit.exe /enum '{current}' 2>&1 | Out-String });
    HardwareValidated=$false;
    Notes=@('Review/redact device identifiers and paths before sharing this report.',
        'Firmware source commit, actual board revision, HDMI wiring, framebuffer lifetime and a working recovery/debug path require operator verification.',
        'No installation, registry change, trust change, reboot, firmware write or upload was performed.')
}
$parent = Split-Path $OutputPath -Parent
if ($parent -and !(Test-Path $parent)) { throw 'The output directory must already exist.' }
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Local platform report: $OutputPath"
if (!$devices.Count) { Write-Warning 'No present ACPI\BCM2712 instance was found. Do not force a generic display binding.' }
