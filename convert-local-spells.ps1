$ErrorActionPreference = 'Stop'
$encoding = [Text.Encoding]::GetEncoding(949)
foreach ($type in @('wizard', 'priest')) {
    $source = Join-Path $PSScriptRoot "local-client/Magic/$type.bin"
    $bytes = [IO.File]::ReadAllBytes($source)
    if ($bytes.Length -ne 234402) { throw "Unexpected spell table size for $type" }
    $checksum = 0
    for ($index = 1; $index -lt $bytes.Length - 1; $index++) {
        $checksum = ($checksum + $bytes[$index]) -band 255
    }
    if ($checksum -ne $bytes[0] -or $checksum -ne $bytes[-1]) {
        throw "Original spell table checksum failed for $type"
    }
    function Read-SpellText([int]$offset, [int]$length, [int]$limit) {
        $text = $encoding.GetString($bytes, $offset, $length).Split([char]0)[0]
        $text = $text.Replace("`t", ' ').Replace("`r", ' ').Replace("`n", ' ').Trim()
        while ($encoding.GetByteCount($text) -gt $limit) {
            $text = $text.Substring(0, $text.Length - 1)
        }
        if (!$text.Length) { return '-' }
        return $text
    }
    $lines = for ($record = 0; $record -lt 200; $record++) {
        $offset = 1 + $record * 1172
        $id = [BitConverter]::ToInt32($bytes, $offset)
        if (!$id) { continue }
        if ($id -lt 0 -or $id -ge 200) { throw "Invalid spell ID in $type" }
        $values = @(
            $id
            (Read-SpellText ($offset + 4) 63 30)
            (Read-SpellText ($offset + 67) 63 30)
        )
        for ($field = 0; $field -lt 4; $field++) {
            $values += [BitConverter]::ToInt32($bytes, $offset + 132 + 4 * $field)
        }
        $values += Read-SpellText ($offset + 148) 1024 1023
        $values -join "`t"
    }
    $backup = Join-Path $PSScriptRoot "local-client/Magic/$type.original.bin"
    if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $source -Destination $backup }
    $destination = Join-Path $PSScriptRoot "local-client/Magic/$type.txt"
    [IO.File]::WriteAllLines($destination, [string[]]$lines, $encoding)
    Write-Output "$type converted with $($lines.Count) spells. Original checksum verified."
}
