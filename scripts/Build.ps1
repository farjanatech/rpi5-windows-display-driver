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
# Never carry an old certificate, catalog or image into a new package.
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
        @{Id='Microsoft.Windows.WDK.ARM64'; Version='10.0.26100.6584'},
        @{Id='Microsoft.Windows.WDK.x64'; Version='10.0.26100.6584'},
        @{Id='Microsoft.Windows.SDK.CPP'; Version='10.0.26100.1'}
    )
    $evidence = @()
    foreach ($spec in $specs) {
        $id = $spec.Id.ToLowerInvariant(); $version = $spec.Version
        $dir = Join-Path $Tools "$id.$version"; $nupkg = "$dir.nupkg"
        $uri = "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg"
        if (!(Test-Path $nupkg)) { Invoke-WebRequest -Uri $uri -OutFile $nupkg }
        Run 'nuget.exe' @('verify','-All',$nupkg,'-NonInteractive','-Verbosity','quiet')
        if (!(Test-Path $dir)) {
            Copy-Item $nupkg "$dir.zip" -Force
            Expand-Archive -Path "$dir.zip" -DestinationPath $dir
            Remove-Item "$dir.zip"
        }
        $hash = (Get-FileHash $nupkg -Algorithm SHA512).Hash
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
    $inf2cat = Find-One $wdkHost 'inf2cat.exe' '[\\/]x64[\\/]'
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
        # Detached CMS authenticates the exact JSON bytes without running any metadata as code.
        $content = [Security.Cryptography.Pkcs.ContentInfo]::new([IO.File]::ReadAllBytes($manifestPath))
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new($content,$true)
        $signer = [Security.Cryptography.Pkcs.CmsSigner]::new($cert)
        $signer.DigestAlgorithm = [Security.Cryptography.Oid]::new('2.16.840.1.101.3.4.2.1')
        $signer.IncludeOption = [Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
        $cms.ComputeSignature($signer)
        [IO.File]::WriteAllBytes((Join-Path $Out 'manifest.p7s'),$cms.Encode())
        Test-LabManifest -ArtifactRoot $Out -ExpectedCommit $sourceCommit -ExpectedThumbprint $cert.Thumbprint | Out-Null
        $tamperTest = Join-Path $Root 'tests/Package.Tests.ps1'
        if (Test-Path $tamperTest) { & $tamperTest -ArtifactRoot $Out -ExpectedCommit $sourceCommit -ExpectedThumbprint $cert.Thumbprint }
    }
    foreach ($helper in @('Lab-Common.ps1','Collect-Platform.ps1','Install-Lab.ps1','Remove-Lab.ps1')) {
        $p = Join-Path $PSScriptRoot $helper
        if (Test-Path $p) { Copy-Item $p $Out -Force }
    }
    Get-ChildItem $Out -Filter *.obj | Remove-Item
    Write-Host 'BUILD/PACKAGE checks passed. This is NOT a Pi hardware test or a production release.'
} finally {
    if ($cert) {
        if ($rootImported) { Remove-Item "Cert:\CurrentUser\Root\$($cert.Thumbprint)" -ErrorAction SilentlyContinue }
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction SilentlyContinue
    }
    Stop-Transcript | Out-Null
}
