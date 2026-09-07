param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
      [string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -BuildDirectory $BuildDirectory
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $projectRoot "build/host-$Configuration" }
& ctest --test-dir $buildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
