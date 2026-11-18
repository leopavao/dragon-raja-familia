$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'local-server')) + '\'
$registration = '"' + (Join-Path $PSScriptRoot 'cadastro-familia.ps1') + '"'
$processes = Get-CimInstance Win32_Process | Where-Object {
    ($_.Name -eq 'powershell.exe' -and $_.CommandLine -and $_.CommandLine.Contains($registration)) -or
    ($_.Name -in @('RajaDB.exe', 'AgentServer.exe', 'ProxyServer.exe', 'MapServer.exe') -and
    $_.ExecutablePath -and
    $_.ExecutablePath.StartsWith($root, [StringComparison]::OrdinalIgnoreCase))
}
foreach ($item in $processes) {
    $process = Get-Process -Id $item.ProcessId -ErrorAction SilentlyContinue
    if ($process) {
        Stop-Process -Id $process.Id
        $process.WaitForExit()
        Write-Output "$($item.Name) stopped."
    }
}
