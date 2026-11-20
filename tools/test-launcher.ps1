$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'artifacts\tests'
[void](New-Item -ItemType Directory -Force -Path $output)
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
$test = Join-Path $output 'LauncherTests.exe'
& $compiler /nologo /target:exe /out:$test /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:System.Web.Extensions.dll (Join-Path $root 'launcher\UpdateEngine.cs') (Join-Path $root 'tests\launcher\UpdateTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Falha ao compilar os testes do launcher.' }
& $test
if ($LASTEXITCODE -ne 0) { throw 'Testes do launcher falharam.' }
$httpTest = Join-Path $output 'GitHubTests.exe'
& $compiler /nologo /target:exe /out:$httpTest /r:System.Net.Http.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:System.Web.Extensions.dll (Join-Path $root 'launcher\UpdateEngine.cs') (Join-Path $root 'launcher\GitHubClient.cs') (Join-Path $root 'tests\launcher\GitHubTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Falha ao compilar os testes HTTP.' }
& $httpTest
if ($LASTEXITCODE -ne 0) { throw 'Testes HTTP falharam.' }
