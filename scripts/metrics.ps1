param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
      [string]$JsonPath = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration | Out-Host
$build = Join-Path $root "build/host-$Configuration"
$runner = Join-Path $build 'motion_controller_closed_loop.exe'
$compiledText = & $runner --configuration-json
if ($LASTEXITCODE -ne 0) { throw 'Configuration metrics failed.' }
$compiled = $compiledText | ConvertFrom-Json
$testText = & ctest --test-dir $build --show-only=json-v1
if ($LASTEXITCODE -ne 0) { throw 'CTest inventory failed.' }
$testInventory = ($testText -join "`n") | ConvertFrom-Json
$catalog = (Get-Command (Join-Path $PSScriptRoot 'demo.ps1')).Parameters['Scenario'].Attributes |
    Where-Object { $_ -is [System.Management.Automation.ValidateSetAttribute] }

function Read-MetricLine([string]$line) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($line, '(\w+)=([^ ]+)')) {
        $key = $match.Groups[1].Value
        $raw = $match.Groups[2].Value
        if ($key -ne 'hash' -and $raw -match '^-?\d+(\.\d+)?$') {
            $values[$key] = [double]::Parse($raw, [Globalization.CultureInfo]::InvariantCulture)
        } else { $values[$key] = $raw }
    }
    return [pscustomobject]$values
}
$measured = [ordered]@{}
foreach ($scenario in @('positive','negative','estop','estop-idle','stall','watchdog','homing-500','homing','homing-4000')) {
    $lines = @(& $runner --quiet --scenario $scenario)
    if ($LASTEXITCODE -ne 0 -or !($lines -match '^PHASE7_OK ')) { throw "Metric scenario failed: $scenario" }
    $prefix = if ($scenario.StartsWith('homing')) { 'HOME_METRIC ' }
              elseif ($scenario -in @('positive','negative')) { 'METRIC ' } else { 'SAFETY_METRIC ' }
    $selected = @($lines | Where-Object { $_.StartsWith($prefix) })
    if ($selected.Count -ne 1) { throw "Expected one $prefix record for $scenario" }
    $measured[$scenario] = Read-MetricLine $selected[0]
}
$report = [ordered]@{
    schema_version = 1
    configuration = $Configuration
    timing_basis = 'simulated milliseconds; no wall-clock or hardware timing'
    compiled_configuration = $compiled
    measured_scenarios = [pscustomobject]$measured
    inventory = [ordered]@{
        configured_tests = @($testInventory.tests).Count
        documented_demos = $catalog.ValidValues.Count
        compiler_warnings = $null
        warning_note = 'Not measured by incremental metrics build; inspect full build logs.'
    }
}
if (!$JsonPath) { $JsonPath = Join-Path $root "build/metrics-$Configuration.json" }
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 $JsonPath
Write-Output 'Real-Time Embedded Motion Controller Metrics'
Write-Output 'Configuration (compiled defaults; frequencies are logical-time rates):'
$compiled | Format-List | Out-String | Write-Output
Write-Output 'Measured scenarios (position/error/overshoot: counts; times: simulated ms; PWM: normalized duty):'
foreach ($name in $measured.Keys) {
    Write-Output "$name : $($measured[$name] | ConvertTo-Json -Compress)"
}
Write-Output "Inventory: $(@($testInventory.tests).Count) configured tests; $($catalog.ValidValues.Count) documented demos."
Write-Output 'Warnings: not measured by this incremental command. Metrics do not imply a full test/demo sweep.'
Write-Output "JSON: $JsonPath"
