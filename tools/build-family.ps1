param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Instale Visual Studio Build Tools com C++ Win32 e Windows SDK.' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
foreach ($name in @('thidchk.dll','ClientLogon.dll')) {
    $source = Join-Path $ClientDirectory $name
    if (!(Test-Path -LiteralPath $source)) { throw "Dependencia do cliente instalado ausente: $name" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $root "dragon2022\$name") -Force
}
Push-Location $root
try {
    $out = Join-Path $root 'artifacts\build'
    [void](New-Item -ItemType Directory -Force -Path $out)
    foreach ($job in @(
        @{ Project='dragon2022\dragon.vcxproj'; Configuration='Releases'; Log='client.log' },
        @{ Project='Mapserver\MapServer.vcxproj'; Configuration='Release'; Log='server.log' },
        @{ Project='RajaDB2022\RajaDB.vcxproj'; Configuration='Release'; Log='database.log' },
        @{ Project='launcher\DragonRajaLauncher.csproj'; Configuration='Release'; Log='launcher.log' }
    )) {
        $log = Join-Path $out $job.Log
        & $msbuild $job.Project /t:Build "/p:Configuration=$($job.Configuration)" /p:Platform=Win32 /p:BuildProjectReferences=false /v:minimal /nologo *> $log
        $code = $LASTEXITCODE
        Get-Content -LiteralPath $log -Tail 4
        if ($code -ne 0) { throw "Build falhou. Consulte $log" }
    }
    Write-Output 'Cliente, MapServer, RajaDB e launcher compilados.'
} finally { Pop-Location }
