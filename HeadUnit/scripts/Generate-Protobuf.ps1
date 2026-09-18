param([Parameter(Mandatory=$true)][string]$Protoc, [Parameter(Mandatory=$true)][string]$SourceRoot, [Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$Protoc = (Resolve-Path -LiteralPath $Protoc).Path
New-Item -ItemType Directory -Force $OutputRoot | Out-Null
$OutputRoot = (Resolve-Path -LiteralPath $OutputRoot).Path
$sources = @(Get-ChildItem -LiteralPath $SourceRoot -Filter *.proto -Recurse | Sort-Object FullName)
$stampPath = Join-Path $OutputRoot 'protobuf.stamp'
$signature = (($sources | ForEach-Object { $_.FullName + ':' + $_.LastWriteTimeUtc.Ticks }) -join "`n") + (Get-Item -LiteralPath $Protoc).LastWriteTimeUtc.Ticks
$hasOutputs = $true
foreach ($source in $sources) {
    $relative = $source.FullName.Substring($SourceRoot.Length + 1)
    foreach ($extension in '.pb.cc','.pb.h') {
        if (!(Test-Path -LiteralPath (Join-Path $OutputRoot ([IO.Path]::ChangeExtension($relative, $extension))))) { $hasOutputs = $false }
    }
}
if ($hasOutputs -and (Test-Path -LiteralPath $stampPath) -and ([IO.File]::ReadAllText($stampPath) -eq $signature)) { exit 0 }
$response = Join-Path $OutputRoot 'protoc.rsp'
$arguments = @("--cpp_out=$OutputRoot", "--proto_path=$SourceRoot")
$arguments += $sources | ForEach-Object { $_.FullName }
[IO.File]::WriteAllLines($response, $arguments, [Text.UTF8Encoding]::new($false))
& $Protoc "@$response"
if ($LASTEXITCODE -ne 0) { throw "protoc failed: $LASTEXITCODE" }
[IO.File]::WriteAllText($stampPath, $signature)
