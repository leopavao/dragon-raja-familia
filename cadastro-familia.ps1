#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$servidor = Join-Path $PSScriptRoot 'local-server'
$config = Get-Content -LiteralPath (Join-Path $servidor 'family-vpn.json') -Raw | ConvertFrom-Json
$gm = Join-Path $servidor 'family-gm.ini'
$ips = @($config.Primos)
if ($ips.Count -lt 1 -or $ips.Count -gt 3 -or @($ips | Sort-Object -Unique).Count -ne $ips.Count) {
    throw 'Configure de um a tres IPs com configurar-vpn-familia.ps1.'
}
$adaptador = Get-NetIPAddress -AddressFamily IPv4 -IPAddress $config.Servidor -ErrorAction SilentlyContinue |
    Where-Object InterfaceAlias -Like 'Tailscale*' | Select-Object -First 1
if (!$adaptador) { throw 'Conecte o Tailscale neste PC primeiro.' }

$connectionString = 'Server=lpc:.\DRLOCAL;Database=TotalDB;Integrated Security=True;Connect Timeout=4'
$conexao = [Data.SqlClient.SqlConnection]::new($connectionString)
$conexao.Open()
$comando = $conexao.CreateCommand()
$comando.CommandText = @'
IF OBJECT_ID(N'dbo.FamilyGameAccounts', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.FamilyGameAccounts (
        slot tinyint NOT NULL PRIMARY KEY CHECK (slot BETWEEN 1 AND 3),
        vpn_ip varchar(15) NOT NULL UNIQUE,
        login_id varchar(15) NOT NULL UNIQUE
    );
END;
'@
[void]$comando.ExecuteNonQuery()

$nomes = @('', '', '')
foreach ($linha in Get-Content -LiteralPath $gm) {
    if ($linha -cmatch '^account([1-3])=([A-Za-z0-9]{3,15})$') {
        $nomes[[int]$Matches[1] - 1] = $Matches[2]
    }
}
$automaticos = @{}
function Atualizar-GM {
    $consulta = $conexao.CreateCommand()
    $consulta.CommandText = 'SELECT slot, login_id FROM dbo.FamilyGameAccounts;'
    $leitor = $consulta.ExecuteReader()
    try {
        while ($leitor.Read()) {
            $posicao = [int]$leitor.GetByte(0) - 1
            $nomes[$posicao] = $leitor.GetString(1)
            $automaticos[$posicao] = $true
        }
    } finally {
        $leitor.Dispose()
        $consulta.Dispose()
    }
    $texto = "[local_admin]`r`naccount1=$($nomes[0])`r`naccount2=$($nomes[1])`r`naccount3=$($nomes[2])`r`n"
    if ([IO.File]::ReadAllText($gm) -cne $texto) {
        $temporario = "$gm.pending"
        [IO.File]::WriteAllText($temporario, $texto, [Text.Encoding]::ASCII)
        [IO.File]::Replace($temporario, $gm, [NullString]::Value)
    }
}

function Registrar-Conta([int]$posicao, [string]$ip, [string]$usuario, [string]$senha) {
    if ($nomes[$posicao] -and !$automaticos.ContainsKey($posicao)) { return 409 }
    $consulta = $conexao.CreateCommand()
    $consulta.CommandTimeout = 5
    try {
        $consulta.CommandText = @'
SET XACT_ABORT ON;
BEGIN TRANSACTION;
DECLARE @status int = 409;
IF EXISTS (
    SELECT 1 FROM dbo.FamilyGameAccounts AS f WITH (UPDLOCK, HOLDLOCK)
    JOIN dbo.Chr_Log_Info AS c ON c.login_id = f.login_id
    WHERE f.vpn_ip = @ip AND f.login_id = @usuario
        AND c.passwd COLLATE Latin1_General_BIN2 = @senha COLLATE Latin1_General_BIN2
)
    SET @status = 201;
ELSE IF NOT EXISTS (
    SELECT 1 FROM dbo.FamilyGameAccounts WITH (UPDLOCK, HOLDLOCK) WHERE slot = @slot OR vpn_ip = @ip
) AND NOT EXISTS (
    SELECT 1 FROM dbo.Chr_Log_Info WITH (UPDLOCK, HOLDLOCK) WHERE login_id = @usuario
)
BEGIN
    INSERT INTO dbo.Chr_Log_Info (login_id, passwd, d_kyulje, d_eday, timeremain, d_regday)
    SELECT @usuario, @senha, d_kyulje, d_eday, timeremain, GETDATE()
    FROM dbo.Chr_Log_Info WHERE login_id = 'leo';
    IF @@ROWCOUNT <> 1
        THROW 50002, 'A conta de referencia nao foi encontrada.', 1;
    INSERT INTO dbo.FamilyGameAccounts (slot, vpn_ip, login_id) VALUES (@slot, @ip, @usuario);
    SET @status = 201;
END;
COMMIT TRANSACTION;
SELECT @status;
'@
        $consulta.Parameters.Add('@slot', [Data.SqlDbType]::TinyInt).Value = [byte]($posicao + 1)
        $consulta.Parameters.Add('@ip', [Data.SqlDbType]::VarChar, 15).Value = $ip
        $consulta.Parameters.Add('@usuario', [Data.SqlDbType]::VarChar, 15).Value = $usuario
        $consulta.Parameters.Add('@senha', [Data.SqlDbType]::NVarChar, 15).Value = $senha
        $status = [int]$consulta.ExecuteScalar()
        if ($status -eq 201) { Atualizar-GM }
        return $status
    } finally {
        $consulta.Dispose()
    }
}

$listener = [Net.HttpListener]::new()
$listener.Prefixes.Add("http://$($config.Servidor):18765/")
try {
    Atualizar-GM
    $listener.Start()
    while ($true) {
        $contexto = $listener.GetContext()
        $pedido = $contexto.Request
        $resposta = $contexto.Response
        try {
            $ip = $pedido.RemoteEndPoint.Address.ToString()
            $posicao = [Array]::IndexOf($ips, $ip)
            if ($posicao -lt 0 -or $pedido.LocalEndPoint.Address.ToString() -ne $config.Servidor) {
                $resposta.StatusCode = 403
            } elseif ($pedido.HttpMethod -ne 'POST' -or $pedido.Url.AbsolutePath -cne '/account') {
                $resposta.StatusCode = 404
            } elseif ($pedido.ContentType -cne 'application/octet-stream' -or
                $pedido.ContentLength64 -lt 12 -or $pedido.ContentLength64 -gt 31) {
                $resposta.StatusCode = 400
            } else {
                $leitor = [IO.StreamReader]::new($pedido.InputStream, [Text.Encoding]::ASCII)
                try {
                    $dados = $leitor.ReadToEnd()
                } finally {
                    $leitor.Dispose()
                }
                if ($dados -cmatch '\A([A-Za-z0-9]{3,15}):([A-Za-z0-9]{8,15})\z') {
                    $resposta.StatusCode = Registrar-Conta $posicao $ip $Matches[1] $Matches[2]
                } else {
                    $resposta.StatusCode = 400
                }
            }
        } catch {
            $resposta.StatusCode = 503
            Write-Warning "Falha de cadastro ($($_.Exception.GetType().Name))."
        } finally {
            $resposta.ContentLength64 = 0
            $resposta.Close()
        }
    }
} finally {
    $listener.Close()
    $conexao.Dispose()
}
