param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
      [string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $projectRoot "build/host-$Configuration" }
& cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
