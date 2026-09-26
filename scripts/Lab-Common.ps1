# SPDX-License-Identifier: GPL-3.0-only
# Shared helpers. Module-path initialization is process-local; no device or policy changes.
Set-StrictMode -Version Latest
# Windows PowerShell launched by a PowerShell 7 process can inherit Core-only
# module paths. Use Windows' built-in/shared module roots and explicitly load
# Utility so script functions such as Get-FileHash are available.
if ($PSVersionTable.PSEdition -eq 'Desktop') {
    $nativeModules=Join-Path $PSHOME 'Modules'
    $sharedModules=Join-Path $env:ProgramFiles 'WindowsPowerShell/Modules'
    $env:PSModulePath=$nativeModules+[IO.Path]::PathSeparator+$sharedModules
    Import-Module (Join-Path $nativeModules 'Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -Force -ErrorAction Stop
}
function Initialize-LabCrypto {
    if ($PSVersionTable.PSEdition -eq 'Core') { Add-Type -AssemblyName System.Security.Cryptography.Pkcs }
    else { Add-Type -AssemblyName System.Security }
}
function Test-LabManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ArtifactRoot,
        [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{40}$')][string]$ExpectedCommit,
        [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{40}$')][string]$ExpectedThumbprint
    )
    Initialize-LabCrypto
    $root = (Resolve-Path -LiteralPath $ArtifactRoot).Path
    foreach ($name in @('manifest.json','manifest.p7s','package')) {
        $item = Get-Item -LiteralPath (Join-Path $root $name) -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not permitted in the package.' }
    }
    $mp = Join-Path $root 'manifest.json'; $sp = Join-Path $root 'manifest.p7s'
    if ((Get-Item $mp).Length -gt 131072 -or (Get-Item $sp).Length -gt 1048576) { throw 'Oversized manifest/signature.' }
    $bytes = [IO.File]::ReadAllBytes($mp)
    $content = [Security.Cryptography.Pkcs.ContentInfo]::new($bytes)
    $cms = [Security.Cryptography.Pkcs.SignedCms]::new($content, $true)
    $cms.Decode([IO.File]::ReadAllBytes($sp))
    $cms.CheckSignature($true)
    if ($cms.SignerInfos.Count -ne 1) { throw 'Expected exactly one manifest signer.' }
    $signer = $cms.SignerInfos[0]
    $certificate = $signer.Certificate
    if (!$certificate -or $certificate.Thumbprint -ine $ExpectedThumbprint -or
        $signer.DigestAlgorithm.Value -ne '2.16.840.1.101.3.4.2.1') { throw 'Unexpected manifest signer or digest.' }
    if ((Get-Date) -lt $certificate.NotBefore -or (Get-Date) -gt $certificate.NotAfter) { throw 'Lab certificate is not currently valid. Rebuild; do not bypass this check.' }
    $manifest = [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF) | ConvertFrom-Json
    if ($manifest.SchemaVersion -ne 1 -or $manifest.Commit -ine $ExpectedCommit -or
        $manifest.Architecture -cne 'ARM64' -or $manifest.TestSigned -ne $true -or
        $manifest.ProductionSigned -ne $false -or $manifest.CertificateThumbprint -ine $ExpectedThumbprint) {
        throw 'Manifest identity, source commit, architecture or signing policy does not match.'
    }
    $expected = @('Rpi5Display.sys','Rpi5Display.inf','Rpi5Display.cat','Rpi5Display.cer')
    $names = @($manifest.Sha256.PSObject.Properties.Name)
    if ($names.Count -ne $expected.Count -or @(Compare-Object $expected $names).Count) { throw 'Unexpected manifest file set.' }
    $actual = @(Get-ChildItem -LiteralPath (Join-Path $root 'package') -Force)
    if ($actual.Count -ne $expected.Count -or @(Compare-Object $expected @($actual.Name)).Count) { throw 'Unexpected package files.' }
    foreach ($name in $expected) {
        $path = Join-Path (Join-Path $root 'package') $name
        $item = Get-Item -LiteralPath $path
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Package entries must be ordinary files.' }
        if ($item.Length -gt 67108864) { throw "Oversized package file: $name" }
        if ($manifest.Sha256.$name -notmatch '^[a-fA-F0-9]{64}$' -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $manifest.Sha256.$name) { throw "Package hash mismatch: $name" }
    }
    $cer = [Security.Cryptography.X509Certificates.X509Certificate2]::new((Join-Path $root 'package/Rpi5Display.cer'))
    if ($cer.Thumbprint -ine $ExpectedThumbprint) { throw 'The install certificate differs from the authenticated manifest signer.' }
    $image = [IO.File]::ReadAllBytes((Join-Path $root 'package/Rpi5Display.sys'))
    if ($image.Length -lt 64 -or $image[0] -ne 77 -or $image[1] -ne 90) { throw 'Invalid driver image.' }
    $pe = [BitConverter]::ToUInt32($image,60)
    if ($pe -gt $image.Length - 26 -or [BitConverter]::ToUInt32($image,$pe) -ne 0x4550 -or
        [BitConverter]::ToUInt16($image,$pe + 4) -ne 0xAA64) { throw 'Driver is not a native ARM64 PE.' }
    Write-Host "Verified source $ExpectedCommit, signer $ExpectedThumbprint and all package hashes. Hardware validation is still required."
    return $manifest
}
function Initialize-LabNative {
    if ('Rpi5Lab.Native' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;
namespace Rpi5Lab {
    public static class Native {
        [StructLayout(LayoutKind.Sequential)] struct CI { public uint Length; public uint Options; }
        [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int cls, ref CI data, uint size, out uint returned);
        [DllImport("cfgmgr32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern uint CM_Locate_DevNodeW(out uint dev, string id, uint flags);
        [DllImport("cfgmgr32.dll", ExactSpelling=true)]
        static extern uint CM_Open_DevNode_Key(uint dev, uint access, uint profile, uint disposition, out IntPtr key, uint flags);
        public static uint CodeIntegrity() {
            CI ci = new CI(); ci.Length = 8; uint returned;
            int status = NtQuerySystemInformation(103, ref ci, 8, out returned);
            if (status < 0 || returned < 8) throw new InvalidOperationException("Cannot query current Code Integrity state: " + status.ToString("X8"));
            return ci.Options;
        }
        public static RegistryKey OpenParameters(string id, bool write) {
            uint dev; IntPtr key;
            uint status = CM_Locate_DevNodeW(out dev, id, 0);
            if (status != 0) throw new InvalidOperationException("Cannot locate exact devnode: " + status);
            status = CM_Open_DevNode_Key(dev, write ? 3u : 1u, 0, write ? 0u : 1u, out key, 0);
            if (status != 0) throw new InvalidOperationException("Cannot open device configuration key: " + status);
            return RegistryKey.FromHandle(new SafeRegistryHandle(key, true));
        }
    }
}
'@
}
function Assert-LabTarget {
    param([Parameter(Mandatory)][string]$DeviceInstanceId)
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
        [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -ne 'Arm64' -or
        [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString() -ne 'Arm64') {
        throw 'Run on Windows using native ARM64 PowerShell, not an x64/x86 emulated shell.'
    }
    $os = Get-CimInstance Win32_OperatingSystem
    if ([int]$os.BuildNumber -lt 22000 -or $os.ProductType -ne 1) { throw 'Windows 11 ARM64 client is required.' }
    $device = Get-PnpDevice -PresentOnly -InstanceId $DeviceInstanceId -ErrorAction Stop
    if ($device.InstanceId -ine $DeviceInstanceId) { throw 'Exact device instance required.' }
    $ids = @((Get-PnpDeviceProperty -InstanceId $DeviceInstanceId -KeyName DEVPKEY_Device_HardwareIds -ErrorAction Stop).Data)
    if ('ACPI\BCM2712' -notin $ids) { throw 'Device does not expose the required ACPI\BCM2712 hardware ID.' }
    return $device
}
function Assert-LabAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (!([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'An elevated native ARM64 PowerShell session is required for installation/removal.'
    }
}
function Assert-LabNoReparseAncestors([string]$Path) {
    $current=[IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item=Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse/junction path rejected: $current" }
        }
        $next=Split-Path $current -Parent
        if ($next -eq $current) { break }; $current=$next
    }
}
function New-LabProtectedDirectory {
    param([string]$Category)
    if ($Category -notin @('Logs','Lab')) { throw 'Invalid lab directory category.' }
    Assert-LabAdministrator
    $root=Join-Path $env:ProgramData 'Rpi5Display'
    Assert-LabNoReparseAncestors $root
    if (Test-Path -LiteralPath $root) {
        $owner=(Get-Acl -LiteralPath $root).GetOwner([Security.Principal.SecurityIdentifier]).Value
        $self=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        if ($owner -notin @('S-1-5-18','S-1-5-32-544',$self)) { throw 'Existing lab root has an unexpected owner.' }
    } else { New-Item -ItemType Directory -Path $root -ErrorAction Stop | Out-Null }
    $acl=Get-Acl -LiteralPath $root
    $acl.SetAccessRuleProtection($true,$false)
    foreach ($rule in @($acl.Access)) { [void]$acl.RemoveAccessRuleSpecific($rule) }
    foreach ($sid in @('S-1-5-18','S-1-5-32-544')) {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($sid),'FullControl','ContainerInherit,ObjectInherit','None','Allow'))
    }
    Set-Acl -LiteralPath $root -AclObject $acl
    $parent=Join-Path $root $Category
    Assert-LabNoReparseAncestors $parent
    if (!(Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
    Set-Acl -LiteralPath $parent -AclObject $acl
    $path=Join-Path $parent ((Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $path -ErrorAction Stop | Out-Null
    return $path
}
function Get-LabInstallDisposition {
    param([int]$PnpExitCode,[bool]$Selected)
    if ($PnpExitCode -eq 3010) { return 'RebootRequired' }
    if ($PnpExitCode -ne 0) { return 'Failed' }
    if (!$Selected) { return 'StagedOnly' }
    return 'CheckSelectedBinary'
}
