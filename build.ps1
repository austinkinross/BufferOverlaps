# Builds BufferOverlaps.exe.

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# The PIX API headers need a newer d3d12.h than the Windows SDK has, so grab the Agility SDK headers once.
$agilitySdkVersion = '1.620.1'
if (-not (Test-Path agility-sdk))
{
    Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/$agilitySdkVersion" -OutFile agility-sdk.zip
    Expand-Archive agility-sdk.zip agility-sdk
    Remove-Item agility-sdk.zip
}

$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
$visualStudio = vswhere -latest -property installationPath
cmd /c "call `"$visualStudio\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl /nologo /std:c++20 /EHsc /Iagility-sdk\build\native\include BufferOverlaps.cpp /link /DELAYLOAD:pixapi.dll"
