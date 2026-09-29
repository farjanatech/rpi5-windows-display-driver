# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param(
    [ValidateSet('Start','Recover')][string]$Mode='Start',
    [ValidateRange(5,60)][int]$IntervalSeconds=10,
    [ValidateRange(16,256)][int]$TraceMaxMB=64,
    [switch]$Elevated
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Test-Admin {
    $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if (!(Test-Admin)) {
    if ($Elevated) { throw 'Administrator elevation failed.' }
    $ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $arguments="-NoLogo -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Mode $Mode -IntervalSeconds $IntervalSeconds -TraceMaxMB $TraceMaxMB -Elevated"
    $child=Start-Process -FilePath $ps -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $child.ExitCode
}

$root=Join-Path $PSScriptRoot 'Logs'
New-Item -ItemType Directory -Path $root -Force | Out-Null

function Invoke-TextCommand([string]$File,[string[]]$Arguments,[string]$Path) {
    try {
        & $File @Arguments 2>&1 | Out-String | Set-Content -LiteralPath $Path -Encoding utf8
        return $LASTEXITCODE
    } catch {
        $_ | Out-String | Set-Content -LiteralPath $Path -Encoding utf8
        return -1
    }
}
function Get-CounterValue($Samples,[string]$Suffix) {
    if (!$Samples) { return $null }
    $wanted=$Suffix.ToLowerInvariant()
    foreach ($sample in $Samples) {
        if ($sample.Path.ToLowerInvariant().EndsWith($wanted)) {
            return [math]::Round([double]$sample.CookedValue,4)
        }
    }
    return $null
}
function Get-ProcessSummary([string]$Name) {
    $items=@(Get-Process -Name $Name -ErrorAction SilentlyContinue)
    if (!$items.Count) {
        return [ordered]@{CpuSeconds=$null; WorkingSetBytes=$null; PrivateBytes=$null; Handles=$null; Threads=$null}
    }
    $cpu=($items | Measure-Object -Property CPU -Sum).Sum
    $ws=($items | Measure-Object -Property WorkingSet64 -Sum).Sum
    $private=($items | Measure-Object -Property PrivateMemorySize64 -Sum).Sum
    $handles=($items | Measure-Object -Property HandleCount -Sum).Sum
    $threads=0
    foreach ($item in $items) { $threads += @($item.Threads).Count }
    return [ordered]@{CpuSeconds=$cpu; WorkingSetBytes=$ws; PrivateBytes=$private; Handles=$handles; Threads=$threads}
}

if ($Mode -eq 'Recover') {
    $session=Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (!$session) { throw "No soak session found under $root" }

    $traceNamePath=Join-Path $session.FullName 'trace-name.txt'
    if (Test-Path -LiteralPath $traceNamePath) {
        $savedTraceName=(Get-Content -LiteralPath $traceNamePath -Raw).Trim()
        if ($savedTraceName) {
            & "$env:SystemRoot\System32\logman.exe" stop $savedTraceName -ets *> $null
        }
    }

    $recovery=Join-Path $session.FullName ('recovery-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $recovery -Force | Out-Null
    "RecoveredUtc=$([DateTime]::UtcNow.ToString('o'))" | Set-Content (Join-Path $recovery 'recovered.txt') -Encoding utf8

    $etl=Join-Path $session.FullName 'driver.etl'
    if (Test-Path -LiteralPath $etl) {
        Invoke-TextCommand "$env:SystemRoot\System32\tracerpt.exe" @(
            $etl,'-o',(Join-Path $recovery 'driver-events.xml'),'-of','XML','-y'
        ) (Join-Path $recovery 'tracerpt.txt') | Out-Null
    }

    try {
        Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddHours(-4)} -ErrorAction Stop |
            Where-Object {
                $_.Level -le 3 -or
                $_.ProviderName -in @('Display','Microsoft-Windows-Kernel-PnP','Microsoft-Windows-WHEA-Logger','Microsoft-Windows-Kernel-Power')
            } |
            Select-Object -First 2000 TimeCreated,Id,LevelDisplayName,ProviderName,Message |
            Format-List | Out-String -Width 300 |
            Set-Content (Join-Path $recovery 'system-events.txt') -Encoding utf8
    } catch {
        $_ | Out-String | Set-Content (Join-Path $recovery 'system-events-error.txt') -Encoding utf8
    }

    try {
        Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddHours(-4)} -ErrorAction Stop |
            Where-Object {
                $_.Level -le 3 -or
                $_.ProviderName -in @('Application Hang','Application Error','Windows Error Reporting','Desktop Window Manager')
            } |
            Select-Object -First 2000 TimeCreated,Id,LevelDisplayName,ProviderName,Message |
            Format-List | Out-String -Width 300 |
            Set-Content (Join-Path $recovery 'application-events.txt') -Encoding utf8
    } catch {
        $_ | Out-String | Set-Content (Join-Path $recovery 'application-events-error.txt') -Encoding utf8
    }

    try {
        Get-PnpDevice -Class Display -ErrorAction Stop |
            Select-Object Status,Class,FriendlyName,InstanceId |
            Format-List | Out-String -Width 300 |
            Set-Content (Join-Path $recovery 'display-devices.txt') -Encoding utf8
    } catch {
        $_ | Out-String | Set-Content (Join-Path $recovery 'display-devices-error.txt') -Encoding utf8
    }

    Invoke-TextCommand "$env:SystemRoot\System32\powercfg.exe" @('/a') (Join-Path $recovery 'power-states.txt') | Out-Null
    Invoke-TextCommand "$env:SystemRoot\System32\powercfg.exe" @('/query','SCHEME_CURRENT','SUB_VIDEO') (Join-Path $recovery 'display-power.txt') | Out-Null
    Invoke-TextCommand "$env:SystemRoot\System32\powercfg.exe" @('/query','SCHEME_CURRENT','SUB_SLEEP') (Join-Path $recovery 'sleep-power.txt') | Out-Null

    $zip=Join-Path $root ('Rpi5Display-Soak-Recovery-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.zip')
    Compress-Archive -Path (Join-Path $session.FullName '*') -DestinationPath $zip -CompressionLevel Optimal -Force
    Write-Host "RECOVERY ZIP: $zip" -ForegroundColor Green
    Write-Host 'Upload this ZIP for analysis.'
    exit 0
}

$sessionName=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$directory=Join-Path $root $sessionName
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$metrics=Join-Path $directory 'metrics.csv'
$traceName='Rpi5DisplaySoak-'+[guid]::NewGuid().ToString('N')
$etl=Join-Path $directory 'driver.etl'
$provider='{8d553ba8-56d6-47a4-a5fb-b567ec131eb3}'

@"
Rpi5Display long-run soak recorder
StartedUtc=$([DateTime]::UtcNow.ToString('o'))
IntervalSeconds=$IntervalSeconds
TraceMaxMB=$TraceMaxMB

Leave this recorder running while the Pi is healthy.
If the machine becomes too laggy to collect logs, reboot only if necessary,
then run Recover-Soak-Watch.cmd. metrics.csv is flushed every sample and the
driver ETW session uses a circular file with a one-second flush timer.
"@ | Set-Content (Join-Path $directory 'soak-info.txt') -Encoding utf8
$traceName | Set-Content (Join-Path $directory 'trace-name.txt') -Encoding ascii

Invoke-TextCommand "$env:SystemRoot\System32\powercfg.exe" @('/getactivescheme') (Join-Path $directory 'active-power-scheme.txt') | Out-Null
Invoke-TextCommand "$env:SystemRoot\System32\powercfg.exe" @('/query','SCHEME_CURRENT','SUB_VIDEO') (Join-Path $directory 'display-power.txt') | Out-Null
Invoke-TextCommand "$env:SystemRoot\System32\powercfg.exe" @('/query','SCHEME_CURRENT','SUB_SLEEP') (Join-Path $directory 'sleep-power.txt') | Out-Null

$logmanArgs=@(
    'create','trace',$traceName,'-ow','-o',$etl,
    '-p',$provider,'0xffffffffffffffff','5',
    '-f','bincirc','-max',[string]$TraceMaxMB,'-bs','64','-nb','16','64',
    '-ft','00:00:01','-ets'
)
$traceStart=Invoke-TextCommand "$env:SystemRoot\System32\logman.exe" $logmanArgs (Join-Path $directory 'trace-start.txt')
$traceActive=($traceStart -eq 0)

[ordered]@{
    StartedUtc=[DateTime]::UtcNow.ToString('o')
    Session=$sessionName
    Directory=$directory
    TraceName=$traceName
    TraceActive=$traceActive
    IntervalSeconds=$IntervalSeconds
} | ConvertTo-Json | Set-Content (Join-Path $directory 'soak-status.json') -Encoding utf8

Write-Host "SOAK DIRECTORY: $directory"
Write-Host "All recorder files remain under: $root"
Write-Host "Driver circular ETW active: $traceActive"
Write-Host 'Leave this window running. If the hang occurs and this becomes unusable, reboot and run Recover-Soak-Watch.cmd.' -ForegroundColor Yellow

$coreCounters=@(
    '\Processor(_Total)\% Processor Time',
    '\Processor(_Total)\% DPC Time',
    '\Processor(_Total)\% Interrupt Time',
    '\System\Processor Queue Length',
    '\Memory\Available MBytes',
    '\Memory\Pool Nonpaged Bytes',
    '\Memory\Pool Paged Bytes'
)
$diskCounters=@(
    '\PhysicalDisk(_Total)\Avg. Disk sec/Transfer',
    '\PhysicalDisk(_Total)\Current Disk Queue Length'
)
$cpuClock=$null
$cpuMax=$null
$cpuLoad=$null
$driverVersion=$null
$problemCode=$null
$deviceStatus=$null
$sampleIndex=0

$sessionClock=[Diagnostics.Stopwatch]::StartNew()
$errorLog=Join-Path $root ('recorder-error-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.txt')
try {
    while ($true) {
        $started=Get-Date
        try {
            $core=$null
            $disk=$null
            try { $core=(Get-Counter -Counter $coreCounters -MaxSamples 1 -ErrorAction Stop).CounterSamples } catch {
                "[$([DateTime]::UtcNow.ToString('o'))] Get-Counter core: $($_.Exception.Message)" |
                    Add-Content -LiteralPath $errorLog -Encoding utf8
            }
            try { $disk=(Get-Counter -Counter $diskCounters -MaxSamples 1 -ErrorAction Stop).CounterSamples } catch {
                "[$([DateTime]::UtcNow.ToString('o'))] Get-Counter disk: $($_.Exception.Message)" |
                    Add-Content -LiteralPath $errorLog -Encoding utf8
            }

            if (($sampleIndex % 6) -eq 0) {
                try {
                    $cpu=Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1
                    $cpuClock=$cpu.CurrentClockSpeed
                    $cpuMax=$cpu.MaxClockSpeed
                    $cpuLoad=$cpu.LoadPercentage
                } catch {
                    "[$([DateTime]::UtcNow.ToString('o'))] CPU CIM: $($_.Exception.Message)" |
                        Add-Content -LiteralPath $errorLog -Encoding utf8
                }
                try {
                    $dev=Get-PnpDevice -PresentOnly -ErrorAction Stop |
                        Where-Object { $_.InstanceId -like 'ACPI\BCM2712\*' } |
                        Select-Object -First 1
                    if ($dev) {
                        $deviceStatus=$dev.Status
                        $props=@(Get-PnpDeviceProperty -InstanceId $dev.InstanceId -ErrorAction Stop)
                        $driverProperty=$props | Where-Object KeyName -eq 'DEVPKEY_Device_DriverVersion' | Select-Object -First 1
                        $problemProperty=$props | Where-Object KeyName -eq 'DEVPKEY_Device_ProblemCode' | Select-Object -First 1
                        if ($driverProperty) { $driverVersion=$driverProperty.Data }
                        if ($problemProperty) { $problemCode=$problemProperty.Data }
                    }
                } catch {
                    "[$([DateTime]::UtcNow.ToString('o'))] PnP query: $($_.Exception.Message)" |
                        Add-Content -LiteralPath $errorLog -Encoding utf8
                }
            }

            $empty=[ordered]@{CpuSeconds=$null; WorkingSetBytes=$null; PrivateBytes=$null; Handles=$null; Threads=$null}
            try { $dwm=Get-ProcessSummary 'dwm' } catch {
                $dwm=$empty
                "[$([DateTime]::UtcNow.ToString('o'))] DWM query: $($_.Exception.Message)" |
                    Add-Content -LiteralPath $errorLog -Encoding utf8
            }
            try { $system=Get-ProcessSummary 'System' } catch {
                $system=$empty
                "[$([DateTime]::UtcNow.ToString('o'))] System query: $($_.Exception.Message)" |
                    Add-Content -LiteralPath $errorLog -Encoding utf8
            }

            $row=[pscustomobject][ordered]@{
                TimestampUtc=[DateTime]::UtcNow.ToString('o')
                RecorderElapsedSeconds=[math]::Round($sessionClock.Elapsed.TotalSeconds,1)
                CpuPercent=(Get-CounterValue $core '\processor(_total)\% processor time')
                DpcPercent=(Get-CounterValue $core '\processor(_total)\% dpc time')
                InterruptPercent=(Get-CounterValue $core '\processor(_total)\% interrupt time')
                ProcessorQueue=(Get-CounterValue $core '\system\processor queue length')
                AvailableMB=(Get-CounterValue $core '\memory\available mbytes')
                NonpagedPoolBytes=(Get-CounterValue $core '\memory\pool nonpaged bytes')
                PagedPoolBytes=(Get-CounterValue $core '\memory\pool paged bytes')
                DiskLatencySeconds=(Get-CounterValue $disk '\physicaldisk(_total)\avg. disk sec/transfer')
                DiskQueue=(Get-CounterValue $disk '\physicaldisk(_total)\current disk queue length')
                CpuClockMHz=$cpuClock
                CpuMaxMHz=$cpuMax
                CpuLoadPercent=$cpuLoad
                DwmCpuSeconds=$dwm.CpuSeconds
                DwmWorkingSetBytes=$dwm.WorkingSetBytes
                DwmPrivateBytes=$dwm.PrivateBytes
                DwmHandles=$dwm.Handles
                DwmThreads=$dwm.Threads
                SystemCpuSeconds=$system.CpuSeconds
                SystemWorkingSetBytes=$system.WorkingSetBytes
                SystemPrivateBytes=$system.PrivateBytes
                SystemHandles=$system.Handles
                SystemThreads=$system.Threads
                DriverVersion=$driverVersion
                ProblemCode=$problemCode
                DeviceStatus=$deviceStatus
            }

            if (!(Test-Path -LiteralPath $metrics)) {
                $row | Export-Csv -LiteralPath $metrics -NoTypeInformation -Encoding UTF8
            } else {
                $row | Export-Csv -LiteralPath $metrics -NoTypeInformation -Encoding UTF8 -Append
            }
            ++$sampleIndex

            if (($sampleIndex % 6) -eq 0) {
                Write-Host ("Recorder alive: {0} samples, {1:n0}s" -f $sampleIndex,$sessionClock.Elapsed.TotalSeconds)
            }
        } catch {
            "[$([DateTime]::UtcNow.ToString('o'))] SAMPLE ERROR: $($_ | Out-String)" |
                Add-Content -LiteralPath $errorLog -Encoding utf8
            Write-Warning "Sample failed but recorder will continue: $($_.Exception.Message)"
        }

        $elapsed=((Get-Date)-$started).TotalSeconds
        $sleep=[math]::Max(1,$IntervalSeconds-[int][math]::Ceiling($elapsed))
        Start-Sleep -Seconds $sleep
    }
} catch {
    "[$([DateTime]::UtcNow.ToString('o'))] FATAL RECORDER ERROR: $($_ | Out-String)" |
        Add-Content -LiteralPath $errorLog -Encoding utf8
    Write-Host "FATAL RECORDER ERROR: $($_.Exception.Message)" -ForegroundColor Red
    throw
} finally {
    if ($traceActive) {
        Invoke-TextCommand "$env:SystemRoot\System32\logman.exe" @('stop',$traceName,'-ets') (Join-Path $directory 'trace-stop.txt') | Out-Null
    }
    "EndedUtc=$([DateTime]::UtcNow.ToString('o'))" | Add-Content (Join-Path $directory 'soak-info.txt') -Encoding utf8
}
