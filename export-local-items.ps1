$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Data
$connection = New-Object Data.SqlClient.SqlConnection 'Server=lpc:.\DRLOCAL;Integrated Security=True;TrustServerCertificate=True;Database=DragonRajaDB'
$connection.Open()
try {
    $types = @('plant', 'mineral', 'herb', 'cook', 'potion', 'tool', 'weapon', 'disposable', 'armor', 'accessory', 'etc')
    for ($index = 0; $index -lt $types.Count; $index++) {
        $command = $connection.CreateCommand()
        $command.CommandText = "EXEC up_get_item_$($types[$index])"
        $adapter = New-Object Data.SqlClient.SqlDataAdapter $command
        $table = New-Object Data.DataTable
        [void]$adapter.Fill($table)
        $lines = foreach ($row in $table.Rows) {
            $values = for ($column = 0; $column -lt $table.Columns.Count; $column++) {
                $value = $row[$column]
                if ($value -is [DBNull]) { $value = 0 }
                $text = [Convert]::ToString($value, [Globalization.CultureInfo]::InvariantCulture)
                $text = $text.Trim().Replace("`t", ' ').Replace("`r", ' ').Replace("`n", ' ')
                if (!$text.Length) { $text = '-' }
                if ($column -in @(1, 2)) {
                    while ([Text.Encoding]::GetEncoding(949).GetByteCount($text) -gt 30) {
                        $text = $text.Substring(0, $text.Length - 1)
                    }
                }
                $text
            }
            $values -join "`t"
        }
        $path = Join-Path $PSScriptRoot "local-client/ItemTable/itemtable$index.txt"
        [IO.File]::WriteAllLines($path, [string[]]$lines, [Text.Encoding]::GetEncoding(949))
        Write-Output "$($types[$index]): $($table.Rows.Count) item rows exported."
    }
} finally { $connection.Close() }
