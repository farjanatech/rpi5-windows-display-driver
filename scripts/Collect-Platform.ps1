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

function Get-AcpiFirmwareTable([uint32]$TableId) {
    if (-not ('Rpi5Lab.FirmwareTable' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Rpi5Lab {
    public static class FirmwareTable {
        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern UInt32 GetSystemFirmwareTable(
            UInt32 provider, UInt32 tableId, IntPtr buffer, UInt32 bufferSize);
    }
}
'@
    }
    $provider = [uint32]0x41435049 # 'ACPI'
    $size = [Rpi5Lab.FirmwareTable]::GetSystemFirmwareTable($provider,$TableId,[IntPtr]::Zero,0)
    if ($size -eq 0) {
        return [ordered]@{Present=$false; Win32Error=[Runtime.InteropServices.Marshal]::GetLastWin32Error()}
    }
    $ptr=[Runtime.InteropServices.Marshal]::AllocHGlobal([int]$size)
    try {
        $got=[Rpi5Lab.FirmwareTable]::GetSystemFirmwareTable($provider,$TableId,$ptr,$size)
        if ($got -eq 0) {
            return [ordered]@{Present=$false; Win32Error=[Runtime.InteropServices.Marshal]::GetLastWin32Error()}
        }
        $bytes=New-Object byte[] $got
        [Runtime.InteropServices.Marshal]::Copy($ptr,$bytes,0,[int]$got)
        [ordered]@{Present=$true; Bytes=$bytes}
    } finally {
        [Runtime.InteropServices.Marshal]::FreeHGlobal($ptr)
    }
}
function Read-R5dgAcpi {
    $raw=Get-AcpiFirmwareTable ([uint32]0x47443552) # bytes "R5DG"
    if (-not $raw.Present) { return $raw }
    $b=[byte[]]$raw.Bytes
    if ($b.Length -lt 160) {
        return [ordered]@{Present=$true; Bytes=$b.Length; ParseError='R5DG shorter than expected 160 bytes'}
    }
    $sum=0
    foreach($v in $b){$sum=($sum+[int]$v)-band 0xff}
    function U32([int]$o){[BitConverter]::ToUInt32($b,$o)}
    function U64([int]$o){[BitConverter]::ToUInt64($b,$o)}
    function Pv([int]$o){
        [ordered]@{
            Base=('0x{0:X16}' -f (U64 $o));
            Control=('0x{0:X8}' -f (U32 ($o+8)));
            VControl=('0x{0:X8}' -f (U32 ($o+12)));
            Horza=('0x{0:X8}' -f (U32 ($o+16)));
            Horzb=('0x{0:X8}' -f (U32 ($o+20)));
            Verta=('0x{0:X8}' -f (U32 ($o+24)));
            Vertb=('0x{0:X8}' -f (U32 ($o+28)));
            Intstat=('0x{0:X8}' -f (U32 ($o+32)))
        }
    }
    [ordered]@{
        Present=$true; Bytes=$b.Length; ChecksumValid=($sum -eq 0);
        Revision=[int]$b[8]; DiagVersion=(U32 36);
        StatusFlags=('0x{0:X8}' -f (U32 40));
        TimingSource=(U32 44); SelectedPixelValve=(U32 48);
        LastStatus=('0x{0:X16}' -f (U64 52));
        FramePeriodNs=(U64 60); DerivedClockKHz=(U32 68);
        ActiveWidth=(U32 72); ActiveHeight=(U32 76);
        HTotal=(U32 80); VTotal=(U32 84);
        PixelValve0=(Pv 88); PixelValve1=(Pv 124)
    }
}
function Read-R5dhAcpi {
    $raw=Get-AcpiFirmwareTable ([uint32]0x48443552) # bytes "R5DH"
    if (-not $raw.Present) { return $raw }
    $b=[byte[]]$raw.Bytes
    $sum=0
    foreach($v in $b){$sum=($sum+[int]$v)-band 0xff}
    [ordered]@{Present=$true; Bytes=$b.Length; ChecksumValid=($sum -eq 0); Revision=if($b.Length -gt 8){[int]$b[8]}else{$null}}
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
                FirmwareTimingValid=$key.GetValue('Rpi5DisplayFirmwareTimingValid',$null);
                FirmwareEdidValid=$key.GetValue('Rpi5DisplayFirmwareEdidValid',$null);
                FirmwareVariableAttributes=$key.GetValue('Rpi5DisplayFirmwareVariableAttributes',$null);
                FirmwareHandoffSource=$key.GetValue('Rpi5DisplayFirmwareHandoffSource',$null);
                FirmwareClockKHz=$key.GetValue('Rpi5DisplayFirmwareClockKHz',$null);
                FirmwareHTotal=$key.GetValue('Rpi5DisplayFirmwareHTotal',$null);
                FirmwareVTotal=$key.GetValue('Rpi5DisplayFirmwareVTotal',$null);
                FirmwareEdidBlocks=$key.GetValue('Rpi5DisplayFirmwareEdidBlocks',$null);
                VSyncAdvertised=$key.GetValue('Rpi5DisplayVSyncAdvertised',$null);
                VSyncHardwareReady=$key.GetValue('Rpi5DisplayVSyncHardwareReady',$null);
                VSyncInterruptEnabled=$key.GetValue('Rpi5DisplayVSyncInterruptEnabled',$null);
                VSyncInterruptEnabledBeforeStop=$key.GetValue('Rpi5DisplayVSyncInterruptEnabledBeforeStop',$null);
                VSyncPhaseSource=$key.GetValue('Rpi5DisplayVSyncPhaseSource',$null);
                VSyncPhaseSourceAtStop=$key.GetValue('Rpi5DisplayVSyncPhaseSourceAtStop',$null);
                VSyncAnchorReady=$key.GetValue('Rpi5DisplayVSyncAnchorReady',$null);
                VSyncAnchorQpcLow=$key.GetValue('Rpi5DisplayVSyncAnchorQpcLow',$null);
                VSyncAnchorQpcHigh=$key.GetValue('Rpi5DisplayVSyncAnchorQpcHigh',$null);
                VSyncProvisionalPhaseReady=$key.GetValue('Rpi5DisplayVSyncProvisionalPhaseReady',$null);
                VSyncProvisionalPhaseUsed=$key.GetValue('Rpi5DisplayVSyncProvisionalPhaseUsed',$null);
                VSyncProvisionalSeedQpcLow=$key.GetValue('Rpi5DisplayVSyncProvisionalSeedQpcLow',$null);
                VSyncProvisionalSeedQpcHigh=$key.GetValue('Rpi5DisplayVSyncProvisionalSeedQpcHigh',$null);
                PixelValveIndex=$key.GetValue('Rpi5DisplayPixelValveIndex',$null);
                PixelValvePhysLow=$key.GetValue('Rpi5DisplayPixelValvePhysLow',$null);
                PixelValvePhysHigh=$key.GetValue('Rpi5DisplayPixelValvePhysHigh',$null);
                VSyncInterruptsLow=$key.GetValue('Rpi5DisplayVSyncInterruptsLow',$null);
                VSyncInterruptsHigh=$key.GetValue('Rpi5DisplayVSyncInterruptsHigh',$null);
                ScanLineQueriesLow=$key.GetValue('Rpi5DisplayScanLineQueriesLow',$null);
                ScanLineQueriesHigh=$key.GetValue('Rpi5DisplayScanLineQueriesHigh',$null);
                VSyncProvisionalQueriesLow=$key.GetValue('Rpi5DisplayVSyncProvisionalQueriesLow',$null);
                VSyncProvisionalQueriesHigh=$key.GetValue('Rpi5DisplayVSyncProvisionalQueriesHigh',$null);
                VSyncControlRequestsLow=$key.GetValue('Rpi5DisplayVSyncControlRequestsLow',$null);
                VSyncControlRequestsHigh=$key.GetValue('Rpi5DisplayVSyncControlRequestsHigh',$null);
                VSyncControlEnableRequestsLow=$key.GetValue('Rpi5DisplayVSyncControlEnableRequestsLow',$null);
                VSyncControlEnableRequestsHigh=$key.GetValue('Rpi5DisplayVSyncControlEnableRequestsHigh',$null);
                VSyncControlDisableRequestsLow=$key.GetValue('Rpi5DisplayVSyncControlDisableRequestsLow',$null);
                VSyncControlDisableRequestsHigh=$key.GetValue('Rpi5DisplayVSyncControlDisableRequestsHigh',$null);
                VSyncControlFallbacksLow=$key.GetValue('Rpi5DisplayVSyncControlFallbacksLow',$null);
                VSyncControlFallbacksHigh=$key.GetValue('Rpi5DisplayVSyncControlFallbacksHigh',$null);
                VSyncLastControlType=$key.GetValue('Rpi5DisplayVSyncLastControlType',$null);
                VSyncLastControlEnable=$key.GetValue('Rpi5DisplayVSyncLastControlEnable',$null);
                VSyncLastControlSyncStatus=$key.GetValue('Rpi5DisplayVSyncLastControlSyncStatus',$null);
                VSyncLastControlSyncReturn=$key.GetValue('Rpi5DisplayVSyncLastControlSyncReturn',$null);
                VSyncLastControlFallback=$key.GetValue('Rpi5DisplayVSyncLastControlFallback',$null);
                VSyncLastControlStatus=$key.GetValue('Rpi5DisplayVSyncLastControlStatus',$null);
                VSyncLastControlPvInten=$key.GetValue('Rpi5DisplayVSyncLastControlPvInten',$null);
                VSyncLastControlPvIntstat=$key.GetValue('Rpi5DisplayVSyncLastControlPvIntstat',$null);
                VSyncLastEnableType=$key.GetValue('Rpi5DisplayVSyncLastEnableType',$null);
                VSyncLastEnableSyncStatus=$key.GetValue('Rpi5DisplayVSyncLastEnableSyncStatus',$null);
                VSyncLastEnableSyncReturn=$key.GetValue('Rpi5DisplayVSyncLastEnableSyncReturn',$null);
                VSyncLastEnableFallback=$key.GetValue('Rpi5DisplayVSyncLastEnableFallback',$null);
                VSyncLastEnableStatus=$key.GetValue('Rpi5DisplayVSyncLastEnableStatus',$null);
                VSyncLastEnablePvInten=$key.GetValue('Rpi5DisplayVSyncLastEnablePvInten',$null);
                VSyncLastEnablePvIntstat=$key.GetValue('Rpi5DisplayVSyncLastEnablePvIntstat',$null);
                VSyncLastEnableActive=$key.GetValue('Rpi5DisplayVSyncLastEnableActive',$null);
                VSyncLastEnableHardwareReady=$key.GetValue('Rpi5DisplayVSyncLastEnableHardwareReady',$null);
                VSyncLastDisableType=$key.GetValue('Rpi5DisplayVSyncLastDisableType',$null);
                VSyncLastDisableSyncStatus=$key.GetValue('Rpi5DisplayVSyncLastDisableSyncStatus',$null);
                VSyncLastDisableSyncReturn=$key.GetValue('Rpi5DisplayVSyncLastDisableSyncReturn',$null);
                VSyncLastDisableFallback=$key.GetValue('Rpi5DisplayVSyncLastDisableFallback',$null);
                VSyncLastDisableStatus=$key.GetValue('Rpi5DisplayVSyncLastDisableStatus',$null);
                VSyncLastDisablePvInten=$key.GetValue('Rpi5DisplayVSyncLastDisablePvInten',$null);
                VSyncLastDisablePvIntstat=$key.GetValue('Rpi5DisplayVSyncLastDisablePvIntstat',$null);
                VSyncPvIntenBeforeStop=$key.GetValue('Rpi5DisplayVSyncPvIntenBeforeStop',$null);
                VSyncPvIntstatBeforeStop=$key.GetValue('Rpi5DisplayVSyncPvIntstatBeforeStop',$null);
                PowerRequestsLow=$key.GetValue('Rpi5DisplayPowerRequestsLow',$null);
                PowerRequestsHigh=$key.GetValue('Rpi5DisplayPowerRequestsHigh',$null);
                AdapterPowerTransitionsLow=$key.GetValue('Rpi5DisplayAdapterPowerTransitionsLow',$null);
                AdapterPowerTransitionsHigh=$key.GetValue('Rpi5DisplayAdapterPowerTransitionsHigh',$null);
                MonitorPowerTransitionsLow=$key.GetValue('Rpi5DisplayMonitorPowerTransitionsLow',$null);
                MonitorPowerTransitionsHigh=$key.GetValue('Rpi5DisplayMonitorPowerTransitionsHigh',$null);
                LastPowerUid=$key.GetValue('Rpi5DisplayLastPowerUid',$null);
                LastPowerState=$key.GetValue('Rpi5DisplayLastPowerState',$null);
                LastPowerAction=$key.GetValue('Rpi5DisplayLastPowerAction',$null);
                LastPowerPreviousState=$key.GetValue('Rpi5DisplayLastPowerPreviousState',$null)
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
    AcpiDisplayHandoff=(Read-Optional { [ordered]@{R5DH=(Read-R5dhAcpi); R5DG=(Read-R5dgAcpi)} });
    DisplayDevices=$adapters;
    VideoControllers=(Read-Optional { Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentBitsPerPixel,CurrentRefreshRate });
    PowerCapabilities=(Read-Optional { & powercfg.exe /a 2>&1 | Out-String });
    SleepPolicy=(Read-Optional { & powercfg.exe /query SCHEME_CURRENT SUB_SLEEP 2>&1 | Out-String });
    DisplayPowerPolicy=(Read-Optional { & powercfg.exe /query SCHEME_CURRENT SUB_VIDEO 2>&1 | Out-String });
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
