param(
    [Parameter(Mandatory)]
    [ValidatePattern('\A[A-Za-z0-9]{3,15}\z')]
    [string]$Usuario,
    [securestring]$SenhaSegura
)

$ErrorActionPreference = 'Stop'
$senhaLidaAqui = !$SenhaSegura
if ($senhaLidaAqui) {
    $SenhaSegura = Read-Host 'Digite uma senha exclusiva para o jogo (8 a 15 letras ou numeros)' -AsSecureString
}
$senhaPtr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($senhaSegura)
$conexao = [Data.SqlClient.SqlConnection]::new('Server=lpc:.\DRLOCAL;Database=TotalDB;Integrated Security=True;Connect Timeout=5')
try {
    $senha = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($senhaPtr)
    if ($senha -cnotmatch '\A[A-Za-z0-9]{8,15}\z') {
        throw 'A senha precisa ter de 8 a 15 letras ou numeros.'
    }

    $conexao.Open()
    $comando = $conexao.CreateCommand()
    $comando.CommandText = @'
SET XACT_ABORT ON;
BEGIN TRANSACTION;
IF EXISTS (SELECT 1 FROM dbo.Chr_Log_Info WITH (UPDLOCK, HOLDLOCK) WHERE login_id = @usuario)
    THROW 50001, 'Esse usuario ja existe.', 1;
INSERT INTO dbo.Chr_Log_Info (login_id, passwd, d_kyulje, d_eday, timeremain, d_regday)
SELECT @usuario, @senha, d_kyulje, d_eday, timeremain, GETDATE()
FROM dbo.Chr_Log_Info
WHERE login_id = 'leo';
IF @@ROWCOUNT <> 1
    THROW 50002, 'A conta de referencia nao foi encontrada.', 1;
COMMIT TRANSACTION;
'@
    $comando.Parameters.Add('@usuario', [Data.SqlDbType]::VarChar, 15).Value = $Usuario
    $comando.Parameters.Add('@senha', [Data.SqlDbType]::NVarChar, 15).Value = $senha
    [void]$comando.ExecuteNonQuery()
    Write-Output "Conta criada: $Usuario. Para liberar o painel GM, coloque o usuario em local-server\family-gm.ini."
}
finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($senhaPtr)
    $conexao.Dispose()
    if ($senhaLidaAqui) { $SenhaSegura.Dispose() }
}
