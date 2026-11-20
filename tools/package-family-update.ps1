param(
    [string]$Version = '1.1.0',
    [Parameter(Mandatory=$true)][string]$ClientDirectory,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root "artifacts\update-$Version" }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Use uma versao numerica, por exemplo 1.0.1.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'A pasta de saida ja existe. Use outra pasta para preservar os pacotes anteriores.' }
$output = New-Item -ItemType Directory -Path $OutputDirectory
$payload = New-Item -ItemType Directory -Path (Join-Path $output.FullName 'payload')
[void](New-Item -ItemType Directory -Path (Join-Path $payload.FullName 'Data'))
Copy-Item -LiteralPath (Join-Path $root 'dragon2022\ReleaseLocal\DragonRaja.exe') -Destination (Join-Path $payload.FullName 'DragonRajaFamilia.exe')
Copy-Item -LiteralPath (Join-Path $root 'launcher\bin\Release\DragonRajaLauncher.exe') -Destination $payload.FullName
Copy-Item -LiteralPath (Join-Path $ClientDirectory 'Data\abilitylimit.bin') -Destination (Join-Path $payload.FullName 'Data\abilitylimit.bin')
$files = @(Get-ChildItem -LiteralPath $payload.FullName -File -Recurse | ForEach-Object {
    @{ path=$_.FullName.Substring($payload.FullName.Length+1).Replace('\','/'); size=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$name = "client-update-$Version.zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($payload.FullName,(Join-Path $output.FullName $name),[IO.Compression.CompressionLevel]::Optimal,$false)
@{ schema=1; version=$Version; package=$name; files=$files } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output.FullName 'family-update.json') -Encoding utf8
$server = New-Item -ItemType Directory -Path (Join-Path $output.FullName 'servidor-para-Leo')
Copy-Item -LiteralPath (Join-Path $root 'docs\TESTAR-ATUALIZACAO.md') -Destination (Join-Path $output.FullName 'LEIA-ME.md')
Copy-Item -LiteralPath (Join-Path $root 'docs\TESTAR-ATUALIZACAO.md') -Destination (Join-Path $server.FullName 'LEIA-ME.md')
$serverExe = Join-Path $server.FullName 'MapServer.exe'
Copy-Item -LiteralPath (Join-Path $root 'Mapserver\Release\MapServer.exe') -Destination $serverExe
Copy-Item -LiteralPath (Join-Path $root 'RajaDB2022\Release\RajaDB.exe') -Destination (Join-Path $server.FullName 'RajaDB.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install-family-server.ps1') -Destination (Join-Path $server.FullName 'instalar-servidor.ps1')
$serverFiles = @('MapServer.exe','RajaDB.exe') | ForEach-Object {
    @{ name=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $server.FullName $_) -Algorithm SHA256).Hash.ToLowerInvariant() }
}
@{ version=$Version; files=@($serverFiles) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $server.FullName 'server-update.json') -Encoding utf8
[IO.Compression.ZipFile]::CreateFromDirectory($server.FullName,(Join-Path $output.FullName "server-update-$Version.zip"),[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Output "Pacotes preparados em $($output.FullName). Nada foi publicado no GitHub."
