param([switch]$LaunchClient, [switch]$SkipFamilyRegistration)
$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot 'local-server'
$familyMode = Test-Path -LiteralPath (Join-Path $root 'family-vpn.json')
$registration = Join-Path $PSScriptRoot 'cadastro-familia.ps1'
if ($familyMode -and !$SkipFamilyRegistration) {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Para iniciar o modo familia, abra o Windows PowerShell como administrador.'
    }
}
$services = @(
    @{ Directory = 'proxy'; Executable = 'ProxyServer.exe' },
    @{ Directory = 'db'; Executable = 'RajaDB.exe' },
    @{ Directory = 'agent'; Executable = 'AgentServer.exe' },
    @{ Directory = 'map'; Executable = 'MapServer.exe' },
    @{ Directory = 'map-main'; Executable = 'MapServer.exe' },
    @{ Directory = 'map-school'; Executable = 'MapServer.exe' }
)
$mapManifest = Join-Path $root 'maps-enabled.json'
if (Test-Path -LiteralPath $mapManifest) {
    $mapServices = Get-Content -LiteralPath $mapManifest -Raw | ConvertFrom-Json
    $services = @($services | Where-Object { $_.Executable -ne 'MapServer.exe' }) + $mapServices
}
foreach ($service in $services) {
    $directory = Join-Path $root $service.Directory
    $executable = Join-Path $directory $service.Executable
    if (!(Test-Path -LiteralPath $executable)) {
        throw "Executable missing: $executable"
    }
    $running = Get-CimInstance Win32_Process -Filter "Name='$($service.Executable)'" |
        Where-Object { $_.ExecutablePath -eq $executable }
    if (!$running) {
        $process = Start-Process -FilePath $executable -WorkingDirectory $directory -WindowStyle Hidden -PassThru
        Write-Output "$($service.Directory) started with PID $($process.Id)."
    }
}
if ($familyMode -and !$SkipFamilyRegistration) {
    $running = Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains('"' + $registration + '"') }
    if (!$running) {
        $registroErros = Join-Path $root 'family-registration-error.log'
        $process = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList "-NoProfile -File `"$registration`"" -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -RedirectStandardError $registroErros -PassThru
        if ($process.WaitForExit(1200)) {
            throw "O cadastro nao iniciou. Consulte $registroErros."
        }
        Write-Output "Cadastro do modo familia iniciado com PID $($process.Id)."
    }
}
if ($LaunchClient) {
    $clientDirectory = Join-Path $PSScriptRoot 'local-client'
    $clientName = if ($familyMode) { 'DragonRajaFamilia.exe' } else { 'DragonRajaLocal.exe' }
    $client = Join-Path $clientDirectory $clientName
    if (!(Test-Path -LiteralPath $client)) {
        throw "Local client missing: $client"
    }
    Start-Process -FilePath $client -WorkingDirectory $clientDirectory
}
