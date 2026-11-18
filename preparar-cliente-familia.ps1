$ErrorActionPreference = 'Stop'
$origem = Join-Path $PSScriptRoot 'local-client'
$destino = Join-Path $PSScriptRoot 'cliente-familia'
if (Test-Path -LiteralPath $destino) {
    throw 'A pasta cliente-familia ja existe. Mova ou renomeie essa pasta antes de gerar outra copia.'
}
$ini = Get-Content -LiteralPath (Join-Path $origem 'dragon.ini') -Raw
if ($ini -notmatch '(?m)^host1=100\.(?:6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7])\.\d{1,3}\.\d{1,3}\r?$') {
    throw 'Execute configurar-vpn-familia.ps1 primeiro.'
}
$dados = @('Achievements', 'Char', 'Character', 'Data', 'Effect', 'Event', 'Item',
    'Itemdata', 'Itemtable', 'Magic', 'Map', 'Object', 'Skill', 'Sound')
foreach ($pasta in $dados) {
    if (!(Test-Path -LiteralPath (Join-Path $origem $pasta) -PathType Container)) {
        throw "Pasta de dados ausente: $pasta"
    }
}
[void](New-Item -ItemType Directory -Path $destino)
foreach ($pasta in $dados) {
    Copy-Item -LiteralPath (Join-Path $origem $pasta) -Destination $destino -Recurse
}
Get-ChildItem -LiteralPath $origem -Filter '*.dll' -File | Copy-Item -Destination $destino
foreach ($arquivo in @('DragonRajaFamilia.exe', 'CrashSender1300.exe', 'dragon.ini', 'beta.ini',
    'ChatColour.ini', 'crashrpt_lang.ini', 'ddraw.ini', 'font.ini', 'client.ver', 'dragon.ico')) {
    Copy-Item -LiteralPath (Join-Path $origem $arquivo) -Destination $destino
}
Write-Output "Pasta pronta para compactar e enviar: $destino"
