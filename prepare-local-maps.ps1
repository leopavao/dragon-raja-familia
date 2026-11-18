$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$maps = @(Get-Content -LiteralPath (Join-Path $root 'local-maps.json') -Raw | ConvertFrom-Json | Where-Object Available)
$manifest = foreach ($map in $maps) {
    $directory = switch ($map.File.ToLowerInvariant()) {
        'barantan' { 'map' }
        'ma-in' { 'map-main' }
        'scholium2' { 'map-school' }
        default { 'map-' + $map.File.ToLowerInvariant() }
    }
    $path = Join-Path $root ('local-server\' + $directory)
    if (!(Test-Path -LiteralPath $path)) {
        [void](New-Item -ItemType Directory -Path $path)
    }
    if (!(Test-Path -LiteralPath (Join-Path $path 'Output'))) {
        [void](New-Item -ItemType Junction -Path (Join-Path $path 'Output') -Target (Join-Path $root 'local-server\map\Output'))
    }
    if (!(Test-Path -LiteralPath (Join-Path $path 'MapServer.exe'))) {
        Copy-Item -LiteralPath (Join-Path $root 'Mapserver\Release\MapServer.exe') -Destination (Join-Path $path 'MapServer.exe')
    }
    $ini = @"
[nation_set]
nation_name=USA
beta_server=1
[network]
path=$root\drserver-03-08-2014\GameServerData
mapname=$($map.File)
[server_info]
own_server_ip_for_server=127.0.0.1
own_server_port_for_server=$($map.Port)
own_server_ip_for_user=
own_server_port_for_user=
primary_proxy_server_ip=127.0.0.1
primary_proxy_server_port=3001
secondary_proxy_server_ip=127.0.0.1
secondary_proxy_server_port=3002
[option]
DisconnectAccelatorUser=1
LogPath=..\LogOutput
[local_admin]
enabled=1
account=leo
"@
    Set-Content -LiteralPath (Join-Path $path 'MapServer.ini') -Value $ini -Encoding ascii
    [pscustomobject]@{ Directory=$directory; Executable='MapServer.exe'; Map=$map.File; Port=$map.Port }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $root 'local-server\maps-enabled.json') -Encoding utf8
$proxyPath = Join-Path $root 'local-server\proxy\ProxyServer.ini'
$proxy = Get-Content -LiteralPath $proxyPath -Raw
if (!(Test-Path -LiteralPath ($proxyPath + '.before-all-maps'))) { Copy-Item -LiteralPath $proxyPath -Destination ($proxyPath + '.before-all-maps') }
$external = [Collections.Generic.List[string]]::new()
$external.Add('[external_server_info]')
$external.Add('num_of_external_servers=' + ($maps.Count + 2))
$ports = @(7001,4001) + @($maps | ForEach-Object Port)
for ($i=0; $i -lt $ports.Count; $i++) {
    $external.Add("external_server_ip$i=127.0.0.1")
    $external.Add("external_server_port$i=$($ports[$i])")
}
$external.Add('external_server_ip_for_user0=127.0.0.1')
$external.Add('external_server_port_for_user0=9001')
$proxy = [regex]::Replace($proxy, '(?s)\[external_server_info\].*$', ($external -join "`r`n") + "`r`n")
Set-Content -LiteralPath $proxyPath -Value $proxy -Encoding ascii
Write-Output "$($maps.Count) maps prepared, including West Glade on 127.0.0.1:5770."
