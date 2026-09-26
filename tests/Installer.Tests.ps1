# SPDX-License-Identifier: GPL-3.0-only
# Build-host tests only. Never install a driver or change target boot/power policy.
param([string]$ArtifactRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $ArtifactRoot 'Lab-Common.ps1')
. (Join-Path $ArtifactRoot 'Diagnostics.ps1')
foreach ($case in @(@(3010,$false,'RebootRequired'),@(3010,$true,'RebootRequired'),@(0,$false,'StagedOnly'),@(0,$true,'CheckSelectedBinary'),@(5,$true,'Failed'))) {
    if ((Get-LabInstallDisposition $case[0] $case[1]) -cne $case[2]) { throw 'Installation-state classification regression.' }
}
if ((ConvertTo-LabArgument 'C:\folder with spaces\') -cne '"C:\folder with spaces\\"') { throw 'Trailing slash argument quoting failed.' }
if ((ConvertTo-LabArgument 'a"b') -cne '"a\"b"') { throw 'Embedded quote handling failed.' }
$dir=Join-Path $env:TEMP ('Rpi5DiagnosticsTest-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir | Out-Null
$trace=$null; $passed=$false
try {
    $p=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $check=Invoke-LabCommand $p @('-NoProfile','-Command',"[Console]::Out.WriteLine('output marker'); [Console]::Error.WriteLine('error marker'); exit 7") $dir 'native-exit' 20
    if ($check.ExitCode -ne 7 -or $check.TimedOut -or $check.Error) { throw 'Native exit-code logging failed.' }
    if ((Get-Content (Join-Path $dir 'native-exit.stdout.txt') -Raw) -notmatch 'output marker' -or
        (Get-Content (Join-Path $dir 'native-exit.stderr.txt') -Raw) -notmatch 'error marker') { throw 'Native output logging failed.' }
    $check=Invoke-LabCommand $p @('-NoProfile','-Command','Start-Sleep -Seconds 5') $dir 'native-timeout' 1
    if (!$check.TimedOut) { throw 'Native timeout did not fail closed.' }
    foreach ($name in @('Install.cmd','Preflight.cmd','Collect-Logs.cmd','Uninstall.cmd')) {
        $cmd=Join-Path $ArtifactRoot $name
        & $cmd '/?'
        if ($LASTEXITCODE -ne 0) { throw "CMD launcher help failed: $name" }
    }
    # Exercise the new trace capture with a USER-MODE synthetic provider, NOT the driver.
    # The distinct GUID prevents these events from being mistaken for hardware evidence.
    Add-Type -TypeDefinition @'
using System.Diagnostics.Tracing;
[EventSource(Name="Rpi5Display.Installer.SmokeTest", Guid="af60a16c-63e7-489b-8d66-46168d1d77fe")]
public sealed class Rpi5TraceSmoke : EventSource {
    public Rpi5TraceSmoke() : base(EventSourceSettings.EtwSelfDescribingEventFormat) {}
    [Event(1,Level=EventLevel.Informational)] public void Marker(string message) { WriteEvent(1,message); }
}
'@
    $provider=[Rpi5TraceSmoke]::new()
    try {
        $trace=Start-LabTrace -Directory $dir -ProviderId 'af60a16c-63e7-489b-8d66-46168d1d77fe'
        if (!$trace.Started) { throw 'ETW test session could not start.' }
        Start-Sleep -Milliseconds 500
        $provider.Marker("INSTALLER_TRACE_SMOKE_TEST")
        Start-Sleep -Milliseconds 500
        Stop-LabTrace $trace $dir; $trace=$null
        $decoded=@(Get-ChildItem $dir -Filter '*-events.xml' | Get-Content -Raw) -join "`n"
        if ($decoded -notmatch 'INSTALLER_TRACE_SMOKE_TEST') { throw 'Synthetic ETW event was not captured and decoded.' }
    } finally { $provider.Dispose() }
    $zip=Complete-LabBundle $dir
    if (!(Test-Path $zip)) { throw 'Support ZIP creation failed.' }
    Remove-Item -LiteralPath $zip -Force
    $passed=$true
    Write-Host 'PASS: installer decisions, CMD help, command/timeout logs, synthetic USER-MODE ETW capture and support ZIP.'
} finally {
    if ($trace) { Stop-LabTrace $trace $dir }
    # Preserve diagnostics on failure for CI artifact review.
    if (!$passed) { Copy-Item $dir (Join-Path $ArtifactRoot 'installer-test-diagnostics') -Recurse -Force }
    Remove-Item $dir -Recurse -Force
}
