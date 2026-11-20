param([Parameter(Mandatory=$true)][string]$Projeto)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath $Projeto).Path
$server = Join-Path $project 'local-server'
if (!(Test-Path -LiteralPath $server -PathType Container)) { throw 'Informe a pasta do projeto que contem local-server.' }
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'server-update.json') -Raw | ConvertFrom-Json
$components = @{}
$files = if ($manifest.files) { @($manifest.files) } else { @([pscustomobject]@{ name='MapServer.exe'; sha256=$manifest.sha256 }) }
foreach ($file in $files) {
    if ($file.name -notin @('MapServer.exe','RajaDB.exe') -or $components.ContainsKey($file.name) -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Manifesto de componentes invalido.' }
    $source = Join-Path $PSScriptRoot $file.name
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.sha256) { throw "SHA-256 diverge para $($file.name). Nenhum servidor foi atualizado." }
    $components[$file.name] = [pscustomobject]@{ Source=$source; Hash=$file.sha256 }
}
$serverPrefix = [IO.Path]::GetFullPath($server).TrimEnd('\') + '\'
$running = @(Get-CimInstance Win32_Process -Filter "Name='MapServer.exe' OR Name='RajaDB.exe'" | Where-Object {
    !$_.ExecutablePath -or $_.ExecutablePath.StartsWith($serverPrefix,[StringComparison]::OrdinalIgnoreCase)
})
if ($running.Count) { throw 'Salve e saia de todos os personagens; pare os servidores com stop-local.ps1 antes de instalar. Nenhum processo foi encerrado.' }
$backupRoot = Join-Path $server '_backup_atualizacoes'
$backupPrefix = [IO.Path]::GetFullPath($backupRoot).TrimEnd('\') + '\'
$targets = @(Get-ChildItem -LiteralPath $server -Filter '*.exe' -File -Recurse | Where-Object {
    $components.ContainsKey($_.Name) -and !$_.FullName.StartsWith($backupPrefix,[StringComparison]::OrdinalIgnoreCase)
} | Sort-Object FullName)
foreach ($name in $components.Keys) {
    if (!@($targets | Where-Object Name -eq $name).Count) { throw "Componente $name nao encontrado na instalacao. Nenhum arquivo foi atualizado." }
}
foreach ($target in $targets) {
    for ($check = $target.FullName; $check -and $check.Length -ge $server.Length; $check = Split-Path $check -Parent) {
        if ((Get-Item -LiteralPath $check).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Pasta redirecionada: instalacao manual necessaria.' }
    }
}
$backup = Join-Path $backupRoot ($manifest.version + '_' + (Get-Date -Format 'yyyy-MM-dd_HHmmss_fff'))
[void](New-Item -ItemType Directory -Path $backup)
foreach ($target in $targets) {
    $relative = $target.FullName.Substring($serverPrefix.Length)
    $old = Join-Path $backup $relative
    [void](New-Item -ItemType Directory -Force -Path (Split-Path $old -Parent))
    Copy-Item -LiteralPath $target.FullName -Destination $old
    if ((Get-FileHash -LiteralPath $old).Hash -ne (Get-FileHash -LiteralPath $target.FullName).Hash) { throw 'Backup diverge. Nenhum servidor foi atualizado.' }
}
$applied = @()
try {
    foreach ($target in $targets) {
        $component = $components[$target.Name]
        $staged = Join-Path $backup ('new-' + [guid]::NewGuid().ToString('N') + '.tmp')
        Copy-Item -LiteralPath $component.Source -Destination $staged
        [IO.File]::Replace($staged, $target.FullName, [NullString]::Value)
        $applied += $target
        if ((Get-FileHash -LiteralPath $target.FullName).Hash -ne $component.Hash) { throw 'Verificacao apos instalacao falhou.' }
    }
} catch {
    $failure = $_.Exception.Message
    foreach ($target in $applied) {
        $old = Join-Path $backup ($target.FullName.Substring($serverPrefix.Length))
        $restore = Join-Path $backup ('restore-' + [guid]::NewGuid().ToString('N') + '.tmp')
        Copy-Item -LiteralPath $old -Destination $restore
        [IO.File]::Replace($restore, $target.FullName, [NullString]::Value)
    }
    throw "Atualizacao cancelada. Arquivos anteriores restaurados de $backup. Causa: $failure"
}
Write-Output "$($targets.Count) executaveis atualizados. Backup: $backup"
Write-Output 'Inicie novamente com start-local.ps1. Configuracoes, contas e banco nao foram alterados.'
