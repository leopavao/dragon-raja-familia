#Requires -RunAsAdministrator
param(
    [Parameter(Mandatory)]
    [ipaddress]$Servidor,
    [Parameter(Mandatory)]
    [ValidateCount(1, 3)]
    [ipaddress[]]$Primos
)

$ErrorActionPreference = 'Stop'
$enderecos = @($Servidor) + $Primos
foreach ($endereco in $enderecos) {
    $bytes = $endereco.GetAddressBytes()
    if ($bytes.Count -ne 4 -or $bytes[0] -ne 100 -or $bytes[1] -lt 64 -or $bytes[1] -gt 127) {
        throw "Use o IPv4 do Tailscale: $endereco"
    }
}
if (@($enderecos | Sort-Object -Unique).Count -ne $enderecos.Count) {
    throw 'Informe enderecos diferentes para o servidor e os primos.'
}

$adaptador = Get-NetIPAddress -AddressFamily IPv4 -IPAddress $Servidor.ToString() -ErrorAction SilentlyContinue |
    Where-Object InterfaceAlias -Like 'Tailscale*' | Select-Object -First 1
if (!$adaptador) { throw 'Conecte o Tailscale neste PC antes de continuar.' }
if (Get-Process ProxyServer, AgentServer, MapServer, DragonRajaLocal, DragonRajaFamilia -ErrorAction SilentlyContinue) {
    throw 'Execute stop-local.ps1 antes de alterar os enderecos do jogo.'
}

$arquivos = @{
    'local-server\proxy\proxyserver.ini' = @('own_server_ip_for_user', 'external_server_ip_for_user0')
    'local-server\agent\agentserver.ini' = @('own_server_ip_for_user')
    'local-client\dragon.ini' = @('host1', 'host2', 'name1_host1', 'name1_host2')
}
$encoding = [Text.Encoding]::GetEncoding(28591)
$mapas = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'local-server\maps-enabled.json') -Raw | ConvertFrom-Json
if (!$mapas.Count) { throw 'O manifesto nao tem mapas.' }
$mapServer = Join-Path $PSScriptRoot 'Mapserver\Release\MapServer.exe'
if (!(Test-Path -LiteralPath $mapServer -PathType Leaf)) { throw 'Executavel atualizado do MapServer ausente.' }
$destinos = foreach ($mapa in $mapas) {
    if ($mapa.Directory -notmatch '\Amap(?:-[A-Za-z0-9_-]+)?\z' -or $mapa.Executable -ne 'MapServer.exe') {
        throw 'Entrada inesperada no manifesto dos mapas.'
    }
    $destino = Join-Path $PSScriptRoot "local-server\$($mapa.Directory)\MapServer.exe"
    if (!(Test-Path -LiteralPath $destino -PathType Leaf)) { throw "Executavel ausente: $destino" }
    $destino
}
$alteracoes = @{}
foreach ($arquivo in $arquivos.Keys) {
    $caminho = Join-Path $PSScriptRoot $arquivo
    $texto = $encoding.GetString([IO.File]::ReadAllBytes($caminho))
    foreach ($chave in $arquivos[$arquivo]) {
        $padrao = '(?m)^' + [regex]::Escape($chave) + '=[^\r\n]*'
        if ([regex]::Matches($texto, $padrao).Count -ne 1) {
            throw "Chave $chave ausente ou duplicada em $arquivo."
        }
        $texto = [regex]::Replace($texto, $padrao, "$chave=$Servidor")
    }
    if ($arquivo -eq 'local-client\dragon.ini') {
        $texto = [regex]::Replace($texto, '(?m)^FamilyRegistration=[^\r\n]*\r?\n', '')
        $texto = [regex]::Replace($texto, '(?m)^\[local\]\r?\n', "[local]`r`nFamilyRegistration=1`r`n")
    }
    $alteracoes[$caminho] = $encoding.GetBytes($texto)
}

$regra = 'DragonRaja-Familia-Tailscale'
$existente = Get-NetFirewallRule -Name $regra -ErrorAction SilentlyContinue
$remotos = @($Primos | ForEach-Object ToString)
if ($existente) {
    $existente | Set-NetFirewallRule -InterfaceAlias $adaptador.InterfaceAlias -LocalAddress $Servidor.ToString() -RemoteAddress $remotos -LocalPort 9001,9004,18765
} else {
    New-NetFirewallRule -Name $regra -DisplayName 'Dragon Raja - familia via Tailscale' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 9001,9004,18765 -InterfaceAlias $adaptador.InterfaceAlias -LocalAddress $Servidor.ToString() -RemoteAddress $remotos | Out-Null
}
foreach ($caminho in $alteracoes.Keys) {
    $backup = "$caminho.before-family-vpn"
    if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $caminho -Destination $backup }
    [IO.File]::WriteAllBytes($caminho, $alteracoes[$caminho])
}
foreach ($destino in $destinos) {
    $backup = "$destino.before-family-vpn"
    if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $destino -Destination $backup }
    Copy-Item -LiteralPath $mapServer -Destination $destino -Force
}
@{ Servidor = $Servidor.ToString(); Primos = $remotos } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'local-server\family-vpn.json') -Encoding ascii
Write-Output "VPN configurada para $Servidor. Agora inicie o jogo com start-local.ps1."
