param([ValidateSet('Debug','Release')][string]$Configuration = 'Release',
      [string]$BuildDirectory = '', [switch]$Clean)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $projectRoot "build/arm-$Configuration" }
foreach ($tool in @('cmake','ninja','arm-none-eabi-gcc','arm-none-eabi-ar','arm-none-eabi-objcopy','arm-none-eabi-size','arm-none-eabi-readelf','arm-none-eabi-nm','arm-none-eabi-objdump')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool missing: $tool" }
}
& cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" '-DMOTION_PLATFORM=stm32f407' "-DCMAKE_TOOLCHAIN_FILE=$projectRoot/cmake/toolchains/arm-none-eabi.cmake"
if ($LASTEXITCODE -ne 0) { throw 'ARM configuration failed.' }
if ($Clean) { & cmake --build $buildDir --target clean; if ($LASTEXITCODE -ne 0) { throw 'ARM clean failed.' } }
& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'ARM build failed.' }
& (Join-Path $PSScriptRoot 'check-arm.ps1') -BuildDirectory $buildDir
