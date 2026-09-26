# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug', [switch]$TestSign)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path $PSScriptRoot -Parent
$Tools = Join-Path $Root '.tools'
$Out = Join-Path $Root "out/ARM64/$Configuration"
$Package = Join-Path $Out 'package'
New-Item $Tools,$Out,$Package -ItemType Directory -Force | Out-Null
Get-ChildItem $Package -File | Remove-Item
Start-Transcript -Path (Join-Path $Out 'build.log') -Force | Out-Null
$cert = $null; $rootImported = $false
function Run([string]$Exe,[string[]]$ArgumentList) {
    & $Exe @ArgumentList 2>&1 | Tee-Object -FilePath (Join-Path $Out 'native.log') -Append
    if ($LASTEXITCODE -ne 0) { throw "$Exe exited with $LASTEXITCODE" }
}
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
        if (!(Test-Path $nupkg)) { Invoke-WebRequest -Uri $uri -OutFile $nupkg }
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
    . (Join-Path $PSScriptRoot 'Compile-Driver.ps1') -Root $Root -Out $Out -Wdk $wdk -Sdk $sdk -Configuration $Configuration
    $sys = Join-Path $Package 'Rpi5Display.sys'
    Copy-Item (Join-Path $Root 'package/Rpi5Display.inf') $Package -Force
    $infverif = Find-One $wdkHost 'infverif.exe' '[\\/]x64[\\/]'
    # Inf2Cat is distributed as an x86 host tool in this WDK; it still catalogs ARM64 targets.
    $inf2cat = Find-One $wdkHost 'inf2cat.exe'
    Run $infverif @('/w','/v',(Join-Path $Package 'Rpi5Display.inf'))
    $signTool = Find-One $sdk 'signtool.exe' '[\\/]x64[\\/]'
    if ($TestSign) {
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Rpi5Display CI LAB ONLY' `
            -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 `
            -HashAlgorithm SHA256 -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddMonths(1)
        $cer = Join-Path $Package 'Rpi5Display.cer'
        Export-Certificate -Cert $cert -FilePath $cer | Out-Null
        Write-Host "LAB certificate thumbprint (pin independently from this run): $($cert.Thumbprint)"
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
    $manifestPath = Join-Path $Out 'manifest.json'
    $manifest | ConvertTo-Json -Depth 8 | Set-Content $manifestPath -Encoding utf8
    if ($TestSign) {
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
    foreach ($helper in @('Lab-Common.ps1','Collect-Platform.ps1','Install-Lab.ps1','Remove-Lab.ps1')) {
        Copy-Item (Join-Path $PSScriptRoot $helper) $Out -Force
    }
    Copy-Item (Join-Path $Root 'LICENSE'),(Join-Path $Root 'THIRD_PARTY_NOTICES.md') $Out -Force
    "Original source and build scripts: https://github.com/farjanatech/rpi5-windows-display-driver/tree/$sourceCommit`nSource archive: https://github.com/farjanatech/rpi5-windows-display-driver/archive/$sourceCommit.zip`nExperimental lab package; NOT HARDWARE VALIDATED." |
        Set-Content (Join-Path $Out 'SOURCE.txt') -Encoding utf8
    Get-ChildItem $Out -Filter *.obj | Remove-Item
    Write-Host 'BUILD/PACKAGE checks passed. This is NOT a Pi hardware test or a production release.'
} finally {
    if ($cert) {
        if ($rootImported) { Remove-Item "Cert:\CurrentUser\Root\$($cert.Thumbprint)" -ErrorAction SilentlyContinue }
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction SilentlyContinue
    }
    Stop-Transcript | Out-Null
}
