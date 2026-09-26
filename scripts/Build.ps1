# SPDX-License-Identifier: GPL-3.0-only
# Explicit MSVC/WDK command-line build: no locally installed WDK or VS extension required.
[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug', [switch]$TestSign)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path $PSScriptRoot -Parent
$Tools = Join-Path $Root '.tools'
$Out = Join-Path $Root "out/ARM64/$Configuration"
$Package = Join-Path $Out 'package'
New-Item $Tools,$Out,$Package -ItemType Directory -Force | Out-Null
Start-Transcript -Path (Join-Path $Out 'build.log') -Force | Out-Null
$cert = $null
$rootImported = $false
try {
    # Pin the VS2022-compatible WDK, plus SDK headers of the same 26100 build family.
    $specs = @(
        @{Id='Microsoft.Windows.WDK.ARM64'; Version='10.0.26100.6584'},
        @{Id='Microsoft.Windows.WDK.x64'; Version='10.0.26100.6584'},
        @{Id='Microsoft.Windows.SDK.CPP'; Version='10.0.26100.1'}
    )
    $evidence = @()
    foreach ($spec in $specs) {
        $id = $spec.Id.ToLowerInvariant(); $version = $spec.Version
        $dir = Join-Path $Tools "$id.$version"
        $zip = "$dir.zip"
        $uri = "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg"
        if (!(Test-Path $zip)) { Invoke-WebRequest -Uri $uri -OutFile $zip }
        $remoteHash = (Invoke-WebRequest -Uri "$uri.sha512").Content
        if ($remoteHash -is [byte[]]) { $remoteHash = [Text.Encoding]::UTF8.GetString($remoteHash) }
        $sha = [Security.Cryptography.SHA512]::Create()
        $stream = [IO.File]::OpenRead($zip)
        try { $actual = [Convert]::ToBase64String($sha.ComputeHash($stream)) } finally { $stream.Dispose(); $sha.Dispose() }
        if ($actual -cne ([string]$remoteHash).Trim()) { throw "NuGet content hash mismatch: $id" }
        if (!(Test-Path $dir)) { Expand-Archive -Path $zip -DestinationPath $dir }
        $evidence += @{Id=$spec.Id; Version=$version; Sha512=$actual}
    }
    $wdk = Join-Path $Tools 'microsoft.windows.wdk.arm64.10.0.26100.6584'
    $wdkHost = Join-Path $Tools 'microsoft.windows.wdk.x64.10.0.26100.6584'
    $sdk = Join-Path $Tools 'microsoft.windows.sdk.cpp.10.0.26100.1'
    function Find-One([string]$Dir,[string]$Name,[string]$Pattern='.') {
        $found = @(Get-ChildItem $Dir -Filter $Name -File -Recurse | Where-Object FullName -Match $Pattern)
        if ($found.Count -ne 1) { throw "Expected one $Name ($Pattern), found $($found.Count) under $Dir" }
        return $found[0].FullName
    }
    function Run([string]$Exe,[string[]]$ArgumentList) {
        & $Exe @ArgumentList
        if ($LASTEXITCODE -ne 0) { throw "$Exe exited with $LASTEXITCODE" }
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationPath
    if (!$vs) { throw 'Visual Studio 2022 C++ tools are required (including ARM64 cross tools).' }
    $vcvars = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
    $envLines = & cmd.exe /d /s /c "`"$vcvars`" -no_logo -arch=arm64 -host_arch=x64 && set"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize MSVC ARM64 tools.' }
    foreach ($line in $envLines) { if ($line -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] } }
    if ($env:VSCMD_ARG_TGT_ARCH -ne 'arm64') { throw 'Wrong compiler target architecture.' }
    $km = Split-Path (Find-One $wdk 'ntddk.h')
    $shared = Split-Path (Find-One $sdk 'ntdef.h')
    $um = Split-Path (Find-One $sdk 'Windows.h')
    $ucrt = Split-Path (Find-One $sdk 'corecrt.h')
    $libs = Split-Path (Find-One $wdk 'ntoskrnl.lib' '[\\/]arm64[\\/]')
    $compile = @('/nologo','/c','/TC','/std:c11','/kernel','/W4','/WX','/Zl','/GS','/guard:cf','/Z7',
        '/D_ARM64_','/D_KERNEL_MODE','/D_WIN32_WINNT=0x0A00','/DWINVER=0x0A00','/DNTDDI_VERSION=0x0A000008',
        '/DDXGKDDI_INTERFACE_VERSION=0x300E',"/I$km","/I$km/crt","/I$shared","/I$um","/I$ucrt")
    if ($Configuration -eq 'Debug') { $compile += '/Od'; $compile += '/DDBG=1' } else { $compile += '/O2' }
    $objects = @()
    foreach ($file in Get-ChildItem (Join-Path $Root 'driver') -Filter *.c) {
        $obj = Join-Path $Out ($file.BaseName + '.obj'); $objects += $obj
        Run 'cl.exe' ($compile + @("/Fo$obj", $file.FullName))
    }
    $sys = Join-Path $Package 'Rpi5Display.sys'
    $pdb = Join-Path $Out 'Rpi5Display.pdb'
    Run 'link.exe' (@('/nologo','/DRIVER','/SUBSYSTEM:NATIVE,10.00','/MACHINE:ARM64','/ENTRY:GsDriverEntry',
        '/NODEFAULTLIB','/DYNAMICBASE','/NXCOMPAT','/INTEGRITYCHECK','/GUARD:CF','/DEBUG:FULL',
        "/OUT:$sys","/PDB:$pdb","/LIBPATH:$libs",'ntoskrnl.lib','hal.lib','displib.lib',
        'BufferOverflowFastFailK.lib','libcntpr.lib') + $objects)
    Copy-Item (Join-Path $Root 'package/Rpi5Display.inf') $Package -Force
    Run 'dumpbin.exe' @('/headers',$sys)
    Run 'dumpbin.exe' @('/imports',$sys)
    Run 'python.exe' @((Join-Path $Root 'scripts/check_pe.py'),$sys)
    $infverif = Find-One $wdkHost 'infverif.exe' '[\\/]x64[\\/]'
    $inf2cat = Find-One $wdkHost 'inf2cat.exe' '[\\/]x64[\\/]'
    Run $infverif @('/w','/v',(Join-Path $Package 'Rpi5Display.inf'))
    $signTool = Find-One $sdk 'signtool.exe' '[\\/]x64[\\/]'
    if ($TestSign) {
        # Disposable build-local identity. Never export a private key or commit a certificate secret.
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Rpi5Display CI LAB ONLY' `
            -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 `
            -HashAlgorithm SHA256 -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddMonths(1)
        $cer = Join-Path $Package 'Rpi5Display.cer'
        Export-Certificate -Cert $cert -FilePath $cer | Out-Null
        Run $signTool @('sign','/fd','SHA256','/s','My','/sha1',$cert.Thumbprint,$sys)
    }
    Run $inf2cat @("/driver:$Package",'/os:10_CO_ARM64','/verbose')
    if ($TestSign) {
        $cat = Join-Path $Package 'Rpi5Display.cat'
        Run $signTool @('sign','/fd','SHA256','/s','My','/sha1',$cert.Thumbprint,$cat)
        Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
        $rootImported = $true
        Run $signTool @('verify','/pa','/v',$sys)
        Run $signTool @('verify','/pa','/v','/c',$cat,$sys)
    }
    $sourceCommit = (& git -C $Root rev-parse HEAD).Trim()
    $files = @{}
    Get-ChildItem $Package -File | ForEach-Object { $files[$_.Name] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
    $manifest = [ordered]@{
        SchemaVersion=1; Commit=$sourceCommit; Configuration=$Configuration; Architecture='ARM64';
        Status='EXPERIMENTAL - NOT HARDWARE VALIDATED'; HardwareTested=$false; ProductionSigned=$false;
        TestSigned=[bool]$TestSign; CompilerVersion=$env:VCToolsVersion; Packages=$evidence; Sha256=$files
    }
    if ($cert) { $manifest['CertificateThumbprint']=$cert.Thumbprint }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $Out 'manifest.json') -Encoding utf8
    # Object files are not part of the downloadable lab package.
    Get-ChildItem $Out -Filter *.obj | Remove-Item
    Write-Host 'BUILD/PACKAGE checks passed. This is NOT a Pi hardware test or a production release.'
} finally {
    if ($cert) {
        if ($rootImported) { Remove-Item "Cert:\CurrentUser\Root\$($cert.Thumbprint)" -ErrorAction SilentlyContinue }
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction SilentlyContinue
    }
    Stop-Transcript | Out-Null
}
