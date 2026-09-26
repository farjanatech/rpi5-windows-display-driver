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
    # An unbound device legitimately lacks some properties. Enumerate once, and
    # distinguish absence from an actual provider/access failure.
    try {
        $properties = @(Get-PnpDeviceProperty -InstanceId $device.InstanceId -ErrorAction Stop)
        $missing = @()
        foreach ($key in @('HardwareIds','CompatibleIds','Service','DriverInfPath','DriverVersion','ProblemCode')) {
            $found = @($properties | Where-Object { $_.KeyName -eq "DEVPKEY_Device_$key" })
            if ($found.Count -eq 1 -and $null -ne $found[0].PSObject.Properties['Data']) { $entry.Properties[$key] = $found[0].Data }
            else { $entry.Properties[$key] = $null; $missing += $key }
        }
        $entry['AbsentProperties'] = $missing
    } catch { $entry['PropertyReadError'] = $_.Exception.Message }
    $entry['ResourcesAndDriverRanking'] = Read-Optional {
        $text = & pnputil.exe /enum-devices /instanceid $device.InstanceId /resources /drivers 2>&1 | Out-String
        @{ExitCode=$LASTEXITCODE; Output=$text}
    }
    $entry['DriverStartDiagnostics'] = Read-Optional {
        Initialize-LabNative
        $key=[Rpi5Lab.Native]::OpenParameters($device.InstanceId,$false)
        try {
            $stage=$key.GetValue('Rpi5DisplayStartStage',$null)
            $status=$key.GetValue('Rpi5DisplayStartStatus',$null)
            if ($null -eq $stage) { return [ordered]@{Recorded=$false} }
            $stageNames=@('NotRecorded','Entered','InterfaceValidated','DeviceInformation',
                'PostOwnership','PostValidated','FramebufferMapped','ShadowAllocated','Completed')
            $stageNumber=[int]$stage
            $stageName=if ($stageNumber -ge 0 -and $stageNumber -lt $stageNames.Count) {
                $stageNames[$stageNumber]
            } else { 'Unknown' }
            $statusBits=$null
            if ($null -ne $status) {
                $statusBits=[BitConverter]::ToUInt32([BitConverter]::GetBytes([int32]$status),0)
            }
            $mapMode=[int]$key.GetValue('Rpi5DisplayFramebufferMapMode',0)
            $mapName=switch ($mapMode) { 1 {'WriteCombined'} 2 {'NonCached'} default {'NotMapped'} }
            [ordered]@{
                Recorded=$true; Stage=$stageNumber; StageName=$stageName;
                StatusHex=if ($null -ne $statusBits) { '0x{0:X8}' -f $statusBits } else { $null };
                FramebufferMapMode=$mapName;
                Width=$key.GetValue('Rpi5DisplayPostWidth',$null);
                Height=$key.GetValue('Rpi5DisplayPostHeight',$null);
                Pitch=$key.GetValue('Rpi5DisplayPostPitch',$null);
                ColorFormat=$key.GetValue('Rpi5DisplayPostColorFormat',$null);
                TargetId=$key.GetValue('Rpi5DisplayPostTargetId',$null);
                AcpiId=$key.GetValue('Rpi5DisplayPostAcpiId',$null);
                PhysicalAddressLow=$key.GetValue('Rpi5DisplayPostPhysLow',$null);
                PhysicalAddressHigh=$key.GetValue('Rpi5DisplayPostPhysHigh',$null);
                EdidBytes=$key.GetValue('Rpi5DisplayEdidBytes',$null);
                TimingFromEdid=$key.GetValue('Rpi5DisplayTimingFromEdid',$null);
                RefreshMilliHz=$key.GetValue('Rpi5DisplayRefreshMilliHz',$null);
                PixelRateHz=$key.GetValue('Rpi5DisplayPixelRateHz',$null);
                HTotal=$key.GetValue('Rpi5DisplayHTotal',$null);
                VTotal=$key.GetValue('Rpi5DisplayVTotal',$null)
            }
        } finally { $key.Dispose() }
    }
    $adapters += $entry
}
$ci = Read-Optional { Initialize-LabNative; [Rpi5Lab.Native]::CodeIntegrity() }
# Version/hash metadata only, never copy Windows binaries into a support bundle.
$graphicsFiles = @()
foreach ($name in @('dxgkrnl.sys','BasicDisplay.sys')) {
    $path = Join-Path $env:SystemRoot ("System32/drivers/" + $name)
    try {
        $item = Get-Item -LiteralPath $path -ErrorAction Stop
        $graphicsFiles += [ordered]@{Name=$name; FileVersion=$item.VersionInfo.FileVersion;
            ProductVersion=$item.VersionInfo.ProductVersion; Bytes=$item.Length;
            Sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256 -ErrorAction Stop).Hash}
    } catch { $graphicsFiles += [ordered]@{Name=$name; Error=$_.Exception.Message} }
}
$report = [ordered]@{
    SchemaVersion=1; CapturedUtc=(Get-Date).ToUniversalTime().ToString('o');
    Windows=@{Caption=$os.Caption; Build=$os.BuildNumber; Version=$os.Version; Architecture=$os.OSArchitecture;
        UBR=(Read-Optional { (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').UBR })};
    Platform=@{Manufacturer=$computer.Manufacturer; Model=$computer.Model; RamBytes=$computer.TotalPhysicalMemory;
        BoardRevision=$BoardRevision; HdmiPort=$HdmiPort};
    Firmware=@{ReportedVersion=$bios.SMBIOSBIOSVersion; ReportedReleaseDate=$bios.ReleaseDate; SourceCommit=$FirmwareCommit};
    WindowsGraphicsFiles=$graphicsFiles;
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
