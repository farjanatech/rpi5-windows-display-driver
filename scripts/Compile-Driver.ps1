# SPDX-License-Identifier: GPL-3.0-only
# Dot-sourced by Build.ps1 after signed toolchain packages have been verified.
param([string]$Root,[string]$Out,[string]$Wdk,[string]$Sdk,[string]$Configuration)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationPath
if (!$vs) { throw 'Visual Studio 2022 C++ tools including ARM64 cross tools are required.' }
$vcvars = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$envLines = & cmd.exe /d /s /c "`"$vcvars`" -no_logo -arch=arm64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize MSVC ARM64 tools.' }
foreach ($line in $envLines) { if ($line -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] } }
if ($env:VSCMD_ARG_TGT_ARCH -ne 'arm64') { throw 'Wrong compiler target architecture.' }
$km = Split-Path (Find-One $Wdk 'ntddk.h')
$wdkShared = Split-Path (Find-One $Wdk 'd3dkmddi.h')
$shared = Split-Path (Find-One $Sdk 'ntdef.h')
$um = Split-Path (Find-One $Sdk 'Windows.h')
$ucrt = Split-Path (Find-One $Sdk 'corecrt.h')
$libs = Split-Path (Find-One $Wdk 'ntoskrnl.lib' '[\\/]arm64[\\/]')
# /kernel defines _KERNEL_MODE itself. Pin WDK shared headers before SDK shared headers.
# /X prevents silently falling back to the runner's unrelated installed Windows Kit.
$vcInclude = Join-Path $env:VCToolsInstallDir 'include'
$compile = @('/nologo','/c','/TC','/std:c11','/kernel','/W4','/WX','/Zl','/GS','/guard:cf','/Z7','/X',
    '/D_ARM64_','/D_WIN32_WINNT=0x0A00','/DWINVER=0x0A00','/DNTDDI_VERSION=0x0A000008',
    '/DDXGKDDI_INTERFACE_VERSION=DXGKDDI_INTERFACE_VERSION_WIN8',"/I$km","/I$km/crt","/I$wdkShared","/I$shared","/I$um","/I$ucrt","/I$vcInclude")
if ($Configuration -eq 'Debug') { $compile += '/Od'; $compile += '/DDBG=1' } else { $compile += '/O2' }
$objects = @(); $failed = @()
foreach ($file in Get-ChildItem (Join-Path $Root 'driver') -Filter *.c) {
    $obj = Join-Path $Out ($file.BaseName + '.obj'); $objects += $obj
    $arguments = $compile + @("/Fo$obj", $file.FullName)
    & cl.exe @arguments 2>&1 | Tee-Object -FilePath (Join-Path $Out ($file.BaseName + '.compiler.log'))
    if ($LASTEXITCODE -ne 0) { $failed += $file.Name }
}
if ($failed.Count) { throw "Compilation failed: $($failed -join ', ')" }
$sys = Join-Path $Out 'package/Rpi5Display.sys'
$pdb = Join-Path $Out 'Rpi5Display.pdb'
Run 'link.exe' (@('/nologo','/DRIVER','/SUBSYSTEM:NATIVE,10.00','/MACHINE:ARM64','/ENTRY:GsDriverEntry',
    '/NODEFAULTLIB','/DYNAMICBASE','/NXCOMPAT','/INTEGRITYCHECK','/GUARD:CF','/DEBUG:FULL',
    "/OUT:$sys","/PDB:$pdb","/LIBPATH:$libs",'ntoskrnl.lib','hal.lib','displib.lib','Aux_Klib.lib',
    'BufferOverflowFastFailK.lib','libcntpr.lib') + $objects)
Run 'dumpbin.exe' @('/headers',$sys)
Run 'dumpbin.exe' @('/imports',$sys)
Run 'python.exe' @((Join-Path $Root 'scripts/check_pe.py'),$sys)

Run 'python.exe' @((Join-Path $Root 'tests/pe_mutation_test.py'),$sys)
