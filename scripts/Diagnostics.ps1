# SPDX-License-Identifier: GPL-3.0-only
# Local-only diagnostic helpers. Dot-sourcing has no side effects.
function ConvertTo-LabArgument([string]$Value) {
    # CommandLineToArgvW-compatible quoting; no shell, no expression evaluation.
    return '"' + [regex]::Replace($Value, '(\\*)("|$)', {
        param($m)
        $slashes = $m.Groups[1].Value
        if ($m.Groups[2].Value -eq '"') { $slashes + $slashes + '\"' }
        else { $slashes + $slashes }
    }) + '"'
}
function Invoke-LabCommand {
    [CmdletBinding()]
    param([string]$FilePath,[string[]]$Arguments,[string]$Directory,
        [ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Label,
        [ValidateRange(1,180)][int]$TimeoutSeconds=30)
    $record = [ordered]@{Label=$Label; Command=$FilePath; Arguments=$Arguments;
        StartedUtc=[DateTime]::UtcNow.ToString('o'); ExitCode=$null; TimedOut=$false; Error=$null}
    $start=[Diagnostics.Stopwatch]::StartNew()
    $process=$null
    try {
        $info=[Diagnostics.ProcessStartInfo]::new()
        $info.FileName=(Get-Command $FilePath -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
        $info.Arguments=(@($Arguments | ForEach-Object { ConvertTo-LabArgument $_ }) -join ' ')
        $info.UseShellExecute=$false; $info.CreateNoWindow=$true
        $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true; $info.RedirectStandardInput=$true
        $process=[Diagnostics.Process]::new(); $process.StartInfo=$info
        if (!$process.Start()) { throw 'Could not start the diagnostic command.' }
        $process.StandardInput.Close()
        $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($TimeoutSeconds*1000)) {
            $record.TimedOut=$true
            # Kill just the process we created, never a shared service or unrelated process.
            $process.Kill(); [void]$process.WaitForExit(5000)
        }
        if ($process.HasExited) { $record.ExitCode=$process.ExitCode }
        foreach ($pair in @(@('stdout',$stdout),@('stderr',$stderr))) {
            if ($pair[1].Wait(5000)) {
                $pair[1].GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $Directory "$Label.$($pair[0]).txt") -Encoding utf8
            }
        }
    } catch { $record.Error=$_.Exception.Message }
    finally {
        if ($process) { $process.Dispose() }
        $start.Stop(); $record['DurationMs']=$start.ElapsedMilliseconds
        $record | ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath (Join-Path $Directory 'commands.jsonl') -Encoding utf8
    }
    Write-Host ("[{0}] exit={1} timeout={2} error={3}" -f $Label,$record.ExitCode,$record.TimedOut,$record.Error)
    return [pscustomobject]$record
}
function Save-LabSnapshot {
    param([string]$Directory,[string]$Stage,[string]$DeviceInstanceId='')
    $target=Join-Path $Directory $Stage
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    try { & (Join-Path $PSScriptRoot 'Collect-Platform.ps1') -OutputPath (Join-Path $target 'platform.json') }
    catch { $_ | Out-String | Set-Content (Join-Path $target 'platform-error.txt') }
    $commands=@(
        @('pnputil.exe',@('/enum-devices','/class','Display','/drivers','/properties','/resources'),'display-devices'),
        @('sc.exe',@('query','Rpi5Display'),'driver-service'),
        @('sc.exe',@('qc','Rpi5Display'),'driver-service-config'),
        @('verifier.exe',@('/querysettings'),'verifier'),
        @('powercfg.exe',@('/a'),'power-states'),
        @('powercfg.exe',@('/query','SCHEME_CURRENT','SUB_SLEEP'),'sleep-policy'),
        @('powercfg.exe',@('/query','SCHEME_CURRENT','SUB_VIDEO'),'display-policy'),
        @('bcdedit.exe',@('/enum','{current}'),'boot-policy')
    )
    if ($DeviceInstanceId) { $commands+= ,@('pnputil.exe',@('/enum-devices','/instanceid',$DeviceInstanceId,'/drivers','/properties','/resources'),'target-device') }
    foreach ($item in $commands) { $null=Invoke-LabCommand $item[0] $item[1] $target $item[2] }
    try {
        Get-CimInstance Win32_PnPSignedDriver -OperationTimeoutSec 15 | Where-Object { $_.DeviceClass -eq 'DISPLAY' } |
            Select-Object DeviceName,DeviceID,DriverVersion,InfName,IsSigned,DriverProviderName,DriverDate |
            ConvertTo-Json -Depth 4 | Set-Content (Join-Path $target 'display-packages.json') -Encoding utf8
    } catch { $_ | Out-String | Set-Content (Join-Path $target 'display-packages-error.txt') }
}
function Save-LabSystemLogs {
    param([string]$Directory)
    $logs=Join-Path $Directory 'windows-logs'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $channels=@('System','Microsoft-Windows-CodeIntegrity/Operational',
        'Microsoft-Windows-Kernel-PnP/Configuration','Microsoft-Windows-DeviceSetupManager/Admin')
    foreach ($channel in $channels) {
        $label=$channel -replace '[^a-zA-Z0-9.-]','_'
        $query='*[System[TimeCreated[timediff(@SystemTime) <= 86400000]]]'
        if ($channel -eq 'System') {
            $query="*[System[TimeCreated[timediff(@SystemTime) <= 86400000] and (Level <= 3 or Provider[@Name='Microsoft-Windows-Kernel-PnP'] or Provider[@Name='Display'])]]"
        }
        $null=Invoke-LabCommand 'wevtutil.exe' @('epl',$channel,(Join-Path $logs "$label.evtx"),"/q:$query",'/ow:true') $logs "$label-export"
        $null=Invoke-LabCommand 'wevtutil.exe' @('qe',$channel,"/q:$query",'/rd:true','/c:200','/f:text') $logs "$label-text"
    }
    # Bounded tail, not an unbounded copy of historical setup activity.
    $source=Join-Path $env:SystemRoot 'INF/setupapi.dev.log'
    try {
        $stream=[IO.File]::Open($source,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        try {
            $count=[int][Math]::Min($stream.Length,2097152)
            [void]$stream.Seek(-$count,[IO.SeekOrigin]::End)
            $bytes=New-Object byte[] $count; $read=0
            while ($read -lt $count) {
                $n=$stream.Read($bytes,$read,$count-$read); if (!$n) { break }; $read+=$n
            }
            [IO.File]::WriteAllBytes((Join-Path $logs 'setupapi.dev.tail.log'),$bytes)
        } finally { $stream.Dispose() }
    } catch { $_ | Out-String | Set-Content (Join-Path $logs 'setupapi-error.txt') }
    $null=Invoke-LabCommand 'dxdiag.exe' @('/whql:off','/t',(Join-Path $logs 'dxdiag.txt')) $logs 'dxdiag' 60
    try {
        @(Get-ChildItem (Join-Path $env:SystemRoot 'Minidump') -Filter *.dmp -ErrorAction Stop |
            Select-Object Name,Length,LastWriteTimeUtc) | ConvertTo-Json | Set-Content (Join-Path $logs 'minidump-inventory.json')
    } catch { 'Minidump inventory unavailable or empty. No dump bytes were collected.' | Set-Content (Join-Path $logs 'minidump-note.txt') }
}
function Start-LabTrace {
    param([string]$Directory,[guid]$ProviderId="8d553ba8-56d6-47a4-a5fb-b567ec131eb3")
    $name='Rpi5Display-'+[guid]::NewGuid().ToString('N')
    $etl=Join-Path $Directory 'driver.etl'
    $result=Invoke-LabCommand 'logman.exe' @('create','trace',$name,'-ow','-o',$etl,
        '-p',('{'+$ProviderId.ToString()+'}'),'0xffffffffffffffff','5',
        '-f','bincirc','-max','64','-bs','64','-nb','16','64','-ft','00:00:01','-ets') $Directory 'trace-start'
    return [pscustomobject]@{Name=$name; Path=$etl; Started=($result.ExitCode -eq 0 -and !$result.TimedOut -and !$result.Error)}
}
function Stop-LabTrace {
    param($Trace,[string]$Directory)
    if (!$Trace -or !$Trace.Started) { return }
    $null=Invoke-LabCommand 'logman.exe' @('query',$Trace.Name,'-ets') $Directory 'trace-status'
    $null=Invoke-LabCommand 'logman.exe' @('stop',$Trace.Name,'-ets') $Directory 'trace-stop'
    # Preserve raw ETL as the authority. Text decoding availability varies by Windows version.
    foreach ($etl in @(Get-ChildItem -LiteralPath $Directory -Filter 'driver*.etl' -File)) {
        $null=Invoke-LabCommand 'tracerpt.exe' @($etl.FullName,'-o',(Join-Path $Directory ($etl.BaseName+'-events.xml')),
            '-of','XML','-y') $Directory 'trace-decode' 60
    }
}
function Complete-LabBundle {
    param([string]$Directory)
    $notice=@'
LOCAL DIAGNOSTICS - REVIEW BEFORE SHARING
This bundle may include usernames, machine/device identifiers, paths and other driver events.
It does not deliberately collect passwords, signing keys, framebuffer pixels or memory-dump bytes.
Nothing is uploaded automatically. A trace is not guaranteed to survive a crash or power loss.
A missing/failed command is recorded in commands.jsonl, not silently treated as successful.
Raw ETL may require Windows Performance Analyzer if XML decoding is incomplete.
Pre/post snapshots and installation output do not prove the loaded driver produces correct pixels.
'@
    $notice | Set-Content (Join-Path $Directory 'PRIVACY-AND-LIMITATIONS.txt')
    $base=(Resolve-Path $Directory).Path
    # -Name returns paths relative to the supplied root and avoids 8.3/long-path
    # substring mistakes when recording the integrity manifest.
    Get-ChildItem -LiteralPath $base -Recurse -File -Name | Where-Object { [IO.Path]::GetFileName($_) -ne 'bundle-hashes.json' } |
        ForEach-Object {
            $file=Get-Item -LiteralPath (Join-Path $base $_) -ErrorAction Stop
            @{Path=$_; Sha256=(Get-FileHash -LiteralPath $file.FullName).Hash; Bytes=$file.Length}
        } |
        ConvertTo-Json -Depth 4 | Set-Content (Join-Path $base 'bundle-hashes.json') -Encoding utf8
    $zip=$base+'.zip'
    Compress-Archive -Path (Join-Path $base '*') -DestinationPath $zip -CompressionLevel Optimal -Force
    Write-Host "SUPPORT BUNDLE: $zip"
    return $zip
}
function Save-PreviousLabEvidence {
    param([string]$Directory)
    # Recover limited evidence after a crash interrupted the previous run's finalizer.
    # No recursive copying of bundles, packages, signing keys or memory dumps.
    $root=Join-Path $env:ProgramData 'Rpi5Display'
    $destination=Join-Path $Directory 'previous-sessions'
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $remaining=134217728L
    # A short (8.3) ancestor path and Get-ChildItem's long path can refer to
    # the same directory. Exclude our unique session NAME, not raw path text.
    $currentSessionName=(Get-Item -LiteralPath $Directory -Force -ErrorAction Stop).Name
    foreach ($category in @('Logs','Lab')) {
        $parent=Join-Path $root $category
        if (!(Test-Path -LiteralPath $parent)) { continue }
        foreach ($session in @(Get-ChildItem -LiteralPath $parent -Directory | Where-Object { $_.Name -ine $currentSessionName } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 2)) {
            try {
                Assert-LabNoReparseAncestors $session.FullName
                $to=Join-Path $destination ($category+'-'+$session.Name)
                New-Item -ItemType Directory -Path $to | Out-Null
                foreach ($file in @(Get-ChildItem -LiteralPath $session.FullName -File | Where-Object { $_.Name -match '^(session\.log|error\.txt|result\.json|install-state\.json|install-result\.json|commands\.jsonl|driver.*\.etl|driver-events\.xml|metrics\.csv|soak-status\.json|soak-info\.txt|trace-name\.txt) })) {
                    Assert-LabNoReparseAncestors $file.FullName
                    if ($file.Length -le $remaining) {
                        Copy-Item -LiteralPath $file.FullName -Destination $to -ErrorAction Stop
                        $remaining-=$file.Length
                    }
                }
            } catch { Write-Warning "Previous-session collection incomplete: $_" }
        }
    }
}
 })) {
                    Assert-LabNoReparseAncestors $file.FullName
                    if ($file.Length -le $remaining) {
                        Copy-Item -LiteralPath $file.FullName -Destination $to -ErrorAction Stop
                        $remaining-=$file.Length
                    }
                }
            } catch { Write-Warning "Previous-session collection incomplete: $_" }
        }
    }
}
