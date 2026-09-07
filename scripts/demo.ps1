param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
      [ValidateSet('nominal','peripherals','burst','rx-overflow','diagnostic-loss','uart-long','uart-tx-failure','plant-open-loop','plant-limits','closed-loop','estop','stall','encoder-failure','watchdog','limit-fault','homing','homing-estop','homing-timeout')]
      [string]$Scenario = 'peripherals')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($Scenario -eq 'closed-loop') {
    & (Join-Path $projectRoot "build/host-$Configuration/motion_controller_closed_loop.exe")
} elseif ($Scenario -in @('estop','stall','encoder-failure','watchdog','limit-fault','homing','homing-estop','homing-timeout')) {
    & (Join-Path $projectRoot "build/host-$Configuration/motion_controller_closed_loop.exe") --scenario $Scenario
} elseif ($Scenario.StartsWith('plant-')) {
    & (Join-Path $projectRoot "build/host-$Configuration/motion_controller_plant.exe") --scenario $Scenario.Substring(6)
} else {
    & (Join-Path $projectRoot "build/host-$Configuration/motion_controller_host.exe") --scenario $Scenario
}
if ($LASTEXITCODE -ne 0) { throw 'Host demo failed.' }
