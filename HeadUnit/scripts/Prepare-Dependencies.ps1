$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$vcpkgRoot = Join-Path $workspace '.tools\vcpkg'
$revision = 'e4e2213e51c0cb6b3610a9d1e08ad27efafa52f4'
if (!(Test-Path -LiteralPath (Join-Path $vcpkgRoot '.git'))) {
    git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone failed' }
    git -C $vcpkgRoot checkout $revision
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg revision checkout failed' }
}
$actual = git -C $vcpkgRoot rev-parse HEAD
if ($actual -ne $revision) { throw "Expected vcpkg $revision, found $actual. Keep the pinned dependency checkout." }
if (!(Test-Path -LiteralPath (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
    & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }
}
$cache = Join-Path $workspace '.tools\vcpkg-cache'
New-Item -ItemType Directory -Force $cache | Out-Null
$env:VCPKG_DEFAULT_BINARY_CACHE = $cache
& (Join-Path $vcpkgRoot 'vcpkg.exe') install boost-asio:x64-windows boost-endian:x64-windows protobuf:x64-windows openssl:x64-windows 'ffmpeg[core,avcodec,swscale]:x64-windows' --disable-metrics
if ($LASTEXITCODE -ne 0) { throw "Dependency build failed: $LASTEXITCODE" }
Write-Host 'Dependencies ready. Open HeadUnit.sln and build Debug or Release x64.'
