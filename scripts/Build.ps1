# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug', [switch]$TestSign)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'The build host requires PowerShell 7 or later.' }
if ($TestSign) {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Test-signature validation requires an elevated dedicated build host. One disposable root certificate is temporarily installed and removed; no target driver is installed.'
    }
}
$Root = Split-Path $PSScriptRoot -Parent
$Tools = Join-Path $Root '.tools'
$Out = Join-Path $Root "out/ARM64/$Configuration"
$Package = Join-Path $Out 'package'
New-Item $Tools,$Out,$Package -ItemType Directory -Force | Out-Null
Get-ChildItem $Package -File | Remove-Item
Start-Transcript -Path (Join-Path $Out 'build.log') -Force | Out-Null
$cert = $null; $rootImported = $false
. (Join-Path $PSScriptRoot 'Run-Checked.ps1')
function Find-One([string]$Dir,[string]$Name,[string]$Pattern='.') {
    $found = @(Get-ChildItem $Dir -Filter $Name -File -Recurse | Where-Object FullName -Match $Pattern)
    if ($found.Count -ne 1) { throw "Expected one $Name ($Pattern), found $($found.Count) under $Dir" }
    return $found[0].FullName
}
try {
    $specs = @(
        @{Id='Microsoft.Windows.WDK.ARM64'; Version='10.0.26100.6584'; Hash='864E7D7E9F2B738436252130C144977CA951560B4A147B99F652BFC87DA23FC94CC839BD53BFE474AE20C06746BB3A2D57021C35377803C39EED7454FA1ECB8E'},
        @{Id='Microsoft.Windows.WDK.x64'; Version='10.0.26100.6584'; Hash='8E175D6819E1303AADDC656BDF64554ED691D0A1E66438D8D09093327D74390A64E3E01285708AF50034F6F05ACE9F278B5323BBCE2A3394865DF21F9F2389FA'},
        @{Id='Microsoft.Windows.SDK.CPP'; Version='10.0.26100.1'; Hash='66F7915A97D02A976E491C88F56C51150E41A54F5767E2B604643A959BA9FFF3E21FE28F14818577F42AAEF16387E8ACAE538E889E5C345E882AE6430A1D44EF'}
    )
    $evidence = @()
    foreach ($spec in $specs) {
        $id = $spec.Id.ToLowerInvariant(); $version = $spec.Version
        $dir = Join-Path $Tools "$id.$version"; $nupkg = "$dir.nupkg"
        $uri = "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg"
        if (!(Test-Path $nupkg)) { Invoke-WebRequest -Uri $uri -OutFile $nupkg -TimeoutSec 120 }
        $hash = (Get-FileHash $nupkg -Algorithm SHA512).Hash
        if ($hash -cne $spec.Hash) { throw "Pinned NuGet content hash mismatch: $id" }
        Run 'nuget.exe' @('verify','-All',$nupkg,'-NonInteractive','-Verbosity','quiet')
        if (!(Test-Path $dir)) {
            Copy-Item $nupkg "$dir.zip" -Force
            Expand-Archive -Path "$dir.zip" -DestinationPath $dir
            Remove-Item "$dir.zip"
        }
        $evidence += @{Id=$spec.Id; Version=$version; Sha512=$hash}
        Write-Host "Verified $($spec.Id) $version SHA512=$hash"
    }
    $wdk = Join-Path $Tools 'microsoft.windows.wdk.arm64.10.0.26100.6584'
    $wdkHost = Join-Path $Tools 'microsoft.windows.wdk.x64.10.0.26100.6584'
    $sdk = Join-Path $Tools 'microsoft.windows.sdk.cpp.10.0.26100.1'
    Write-Host 'PHASE: compile and link'
    . (Join-Path $PSScriptRoot 'Compile-Driver.ps1') -Root $Root -Out $Out -Wdk $wdk -Sdk $sdk -Configuration $Configuration
    $sys = Join-Path $Package 'Rpi5Display.sys'
    Copy-Item (Join-Path $Root 'package/Rpi5Display.inf') $Package -Force
    $infverif = Find-One $wdkHost 'infverif.exe' '[\\/]x64[\\/]'
    $inf2cat = Find-One $wdkHost 'inf2cat.exe'
    Write-Host 'PHASE: INF validation'
    Run $infverif @('/w','/v',(Join-Path $Package 'Rpi5Display.inf'))
    $signTool = Find-One $sdk 'signtool.exe' '[\\/]x64[\\/]'
    if ($TestSign) {
        Write-Host 'PHASE: create disposable lab signing identity'
        # Backdate the disposable signer by one day. The physical Pi can have
        # modest RTC/NTP skew relative to the CI VM; a certificate whose NotBefore
        # equals the CI wall clock can otherwise be rejected before installation.
        # Keep the lifetime short and continue to pin the exact thumbprint.
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Rpi5Display CI LAB ONLY' `
            -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 `
            -HashAlgorithm SHA256 -KeyExportPolicy NonExportable `
            -NotBefore (Get-Date).AddDays(-1) -NotAfter (Get-Date).AddMonths(1)
        $cer = Join-Path $Package 'Rpi5Display.cer'
        Export-Certificate -Cert $cert -FilePath $cer | Out-Null
        Write-Host "LAB certificate thumbprint (pin independently from this run): $($cert.Thumbprint)"
        Run $signTool @('sign','/fd','SHA256','/s','My','/sha1',$cert.Thumbprint,$sys)
    }
    Write-Host 'PHASE: catalog generation'
    Run $inf2cat @("/driver:$Package",'/os:10_CO_ARM64','/verbose')
    if ($TestSign) {
        $cat = Join-Path $Package 'Rpi5Display.cat'
        Run $signTool @('sign','/fd','SHA256','/s','My','/sha1',$cert.Thumbprint,$cat)
        Write-Host 'PHASE: temporary dedicated BUILD-HOST LocalMachine root trust'
        # CurrentUser root insertion displays a trust UI even through certutil -f.
        # CI is an elevated disposable VM. Temporarily use its machine store,
        # then remove exactly this newly generated certificate in finally.
        # This never modifies the physical Pi, test-signing mode, or Secure Boot.
        $rootImported = $true
        Run 'certutil.exe' @('-f','-addstore','Root',$cer)
        Run $signTool @('verify','/pa','/v',$sys)
        Run $signTool @('verify','/pa','/v','/c',$cat,$sys)
    }
    $sourceCommit = (& git -C $Root rev-parse HEAD).Trim()
    Write-Host "BUILT SOURCE COMMIT: $sourceCommit"
    $files = @{}
    Get-ChildItem $Package -File | ForEach-Object { $files[$_.Name] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
    $manifest = [ordered]@{
        SchemaVersion=1; Commit=$sourceCommit; Configuration=$Configuration; Architecture='ARM64';
        Status='EXPERIMENTAL - NOT HARDWARE VALIDATED'; HardwareTested=$false; ProductionSigned=$false;
        TestSigned=[bool]$TestSign; CompilerVersion=$env:VCToolsVersion; Packages=$evidence; Sha256=$files
    }
    if ($cert) { $manifest['CertificateThumbprint']=$cert.Thumbprint }
    $manifestPath = Join-Path $Out 'manifest.json'
    $manifest | ConvertTo-Json -Depth 8 | Set-Content $manifestPath -Encoding utf8
    if ($TestSign) {
        Write-Host 'PHASE: authenticate and test package metadata'
        . (Join-Path $PSScriptRoot 'Lab-Common.ps1')
        Initialize-LabCrypto
        $content = [Security.Cryptography.Pkcs.ContentInfo]::new([IO.File]::ReadAllBytes($manifestPath))
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new($content,$true)
        $signer = [Security.Cryptography.Pkcs.CmsSigner]::new($cert)
        $signer.DigestAlgorithm = [Security.Cryptography.Oid]::new('2.16.840.1.101.3.4.2.1')
        $signer.IncludeOption = [Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
        $cms.ComputeSignature($signer)
        [IO.File]::WriteAllBytes((Join-Path $Out 'manifest.p7s'),$cms.Encode())
        Test-LabManifest -ArtifactRoot $Out -ExpectedCommit $sourceCommit -ExpectedThumbprint $cert.Thumbprint | Out-Null
        & (Join-Path $Root 'tests/Package.Tests.ps1') -ArtifactRoot $Out -ExpectedCommit $sourceCommit -ExpectedThumbprint $cert.Thumbprint
    }
    foreach ($helper in @('Lab-Common.ps1','Collect-Platform.ps1','Install-Lab.ps1','Remove-Lab.ps1','Diagnostics.ps1','Run-Lab.ps1','Install.cmd','Preflight.cmd','Collect-Logs.cmd','Uninstall.cmd','Soak-Watch.ps1','Start-Soak-Watch.cmd','Recover-Soak-Watch.cmd')) {
        Copy-Item (Join-Path $PSScriptRoot $helper) $Out -Force
    }
    if ($TestSign) {
        [ordered]@{Commit=$sourceCommit; Thumbprint=$cert.Thumbprint} | ConvertTo-Json |
            Set-Content (Join-Path $Out 'Package-Pin.json') -Encoding utf8
        & (Join-Path $Root 'tests/Installer.Tests.ps1') -ArtifactRoot $Out
    }
    Run 'git.exe' @('-C',$Root,'archive','--format=zip',"--output=$Out/source.zip",'HEAD')
    Copy-Item (Join-Path $Root 'docs/CMD_INSTALLER.md') (Join-Path $Out 'READ-ME-FIRST.md') -Force
    Copy-Item (Join-Path $Root 'LICENSE'),(Join-Path $Root 'THIRD_PARTY_NOTICES.md') $Out -Force
    "Original source and build scripts: https://github.com/farjanatech/rpi5-windows-display-driver/tree/$sourceCommit`nSource archive: https://github.com/farjanatech/rpi5-windows-display-driver/archive/$sourceCommit.zip`nExperimental lab package; NOT HARDWARE VALIDATED." |
        Set-Content (Join-Path $Out 'SOURCE.txt') -Encoding utf8
    Get-ChildItem $Out -Filter *.obj | Remove-Item
    Write-Host 'BUILD/PACKAGE checks passed. This is NOT a Pi hardware test or a production release.'
} finally {
    if ($cert) {
        if ($rootImported) { Remove-Item "Cert:\LocalMachine\Root\$($cert.Thumbprint)" -Force -ErrorAction SilentlyContinue }
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction SilentlyContinue
    }
    Stop-Transcript | Out-Null
}
