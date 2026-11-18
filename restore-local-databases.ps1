$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Data
$connection = New-Object Data.SqlClient.SqlConnection 'Server=lpc:.\DRLOCAL;Integrated Security=True;TrustServerCertificate=True'
$connection.Open()
try {
    function Read-SqlTable([string]$sql) {
        $command = $connection.CreateCommand()
        $command.CommandText = $sql
        $command.CommandTimeout = 300
        $adapter = New-Object Data.SqlClient.SqlDataAdapter $command
        $table = New-Object Data.DataTable
        [void]$adapter.Fill($table)
        return ,$table
    }
    $paths = Read-SqlTable "SELECT CONVERT(nvarchar(4000), SERVERPROPERTY('InstanceDefaultDataPath')) AS DataPath, CONVERT(nvarchar(4000), SERVERPROPERTY('InstanceDefaultLogPath')) AS LogPath"
    foreach ($name in @('DragonRajaDB', 'TotalDB', 'ChrLogDB')) {
        $existing = Read-SqlTable "SELECT name FROM sys.databases WHERE name=N'$name'"
        if ($existing.Rows.Count) {
            Write-Output "$name already exists. Skipping restore."
            continue
        }
        $backup = "C:/ProgramData/DragonRajaLocal/backups/$name.bak"
        $headers = Read-SqlTable "RESTORE HEADERONLY FROM DISK=N'$backup'"
        $latest = $headers.Rows | Where-Object BackupType -eq 1 | Sort-Object BackupFinishDate -Descending | Select-Object -First 1
        $position = [int]$latest.Position
        $files = Read-SqlTable "RESTORE FILELISTONLY FROM DISK=N'$backup' WITH FILE=$position"
        $moves = foreach ($file in $files.Rows) {
            $logical = $file.LogicalName.Replace("'", "''")
            $base = $paths.Rows[0].DataPath
            $extension = 'mdf'
            if ($file.Type -eq 'L') {
                $base = $paths.Rows[0].LogPath
                $extension = 'ldf'
            }
            $destination = (Join-Path $base "${name}_$($file.FileId).$extension").Replace("'", "''")
            "MOVE N'$logical' TO N'$destination'"
        }
        $command = $connection.CreateCommand()
        $command.CommandTimeout = 600
        $command.CommandText = "RESTORE DATABASE [$name] FROM DISK=N'$backup' WITH FILE=$position, " + ($moves -join ', ') + ', RECOVERY'
        [void]$command.ExecuteNonQuery()
        Write-Output "$name restored from backup $position."
    }
    (Read-SqlTable "SELECT name, state_desc FROM sys.databases WHERE name IN ('DragonRajaDB','TotalDB','ChrLogDB')").Rows | Format-Table
} finally {
    $connection.Close()
}
