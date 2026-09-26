# SPDX-License-Identifier: GPL-3.0-only
# Negative tests run against disposable artifact copies. No driver installation/trust changes.
param([string]$ArtifactRoot,[string]$ExpectedCommit,[string]$ExpectedThumbprint)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/Lab-Common.ps1')
Initialize-LabNative  # Compile the P/Invoke declarations, but do not open or change any device.
$null=Test-LabManifest -ArtifactRoot $ArtifactRoot -ExpectedCommit $ExpectedCommit -ExpectedThumbprint $ExpectedThumbprint
function Must-Reject([scriptblock]$Operation,[string]$Name) {
    $rejected=$false
    try { & $Operation | Out-Null } catch { $rejected=$true }
    if (!$rejected) { throw "FAIL: accepted $Name" }
    Write-Host "PASS: rejected $Name"
}
Must-Reject { Test-LabManifest $ArtifactRoot ('0'*40) $ExpectedThumbprint } 'wrong source commit'
Must-Reject { Test-LabManifest $ArtifactRoot $ExpectedCommit ('0'*40) } 'wrong signer pin'
$dir=Join-Path $env:TEMP ('Rpi5PackageTest-'+[guid]::NewGuid().ToString())
New-Item $dir -ItemType Directory | Out-Null
try {
    Copy-Item (Join-Path $ArtifactRoot 'package') $dir -Recurse
    Copy-Item (Join-Path $ArtifactRoot 'manifest.json'),(Join-Path $ArtifactRoot 'manifest.p7s') $dir
    foreach ($name in @('Rpi5Display.inf','Rpi5Display.sys','Rpi5Display.cat','Rpi5Display.cer')) {
        $path=Join-Path $dir "package/$name"; $original=[IO.File]::ReadAllBytes($path)
        [IO.File]::WriteAllBytes($path,($original + [byte]1))
        Must-Reject { Test-LabManifest $dir $ExpectedCommit $ExpectedThumbprint } "tampered $name"
        [IO.File]::WriteAllBytes($path,$original)
    }
    $path=Join-Path $dir 'manifest.json'; $original=[IO.File]::ReadAllBytes($path)
    [IO.File]::WriteAllBytes($path,($original + [byte]32))
    Must-Reject { Test-LabManifest $dir $ExpectedCommit $ExpectedThumbprint } 'tampered authenticated metadata'
    [IO.File]::WriteAllBytes($path,$original)
    $path=Join-Path $dir 'manifest.p7s'; $original=[IO.File]::ReadAllBytes($path)
    $bad=[byte[]]$original.Clone(); $bad[$bad.Length-1]=$bad[$bad.Length-1] -bxor 1
    [IO.File]::WriteAllBytes($path,$bad)
    Must-Reject { Test-LabManifest $dir $ExpectedCommit $ExpectedThumbprint } 'tampered detached signature'
    [IO.File]::WriteAllBytes($path,$original)
    Set-Content (Join-Path $dir 'package/unexpected.exe') 'not a package member'
    Must-Reject { Test-LabManifest $dir $ExpectedCommit $ExpectedThumbprint } 'unexpected package member'
} finally { Remove-Item $dir -Recurse -Force }
Write-Host 'PASS: package identity, hash and signature negative tests. No hardware was touched.'
