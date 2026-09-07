param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$demo = Join-Path $PSScriptRoot 'demo.ps1'
# Use demo.ps1's validated public catalog; do not maintain a second scenario list.
$catalog = (Get-Command $demo).Parameters['Scenario'].Attributes |
    Where-Object { $_ -is [System.Management.Automation.ValidateSetAttribute] }
$root = Split-Path -Parent $PSScriptRoot
foreach ($scenario in $catalog.ValidValues) {
    if ($scenario -eq 'peripherals') { $output = & $demo -Configuration $Configuration 2>&1 }
    else { $output = & $demo -Configuration $Configuration -Scenario $scenario 2>&1 }
    $output | Set-Content -Encoding utf8 (Join-Path $root "build/demo-$Configuration-$scenario.log")
    Write-Output "DEMO_PASS $scenario"
}
Write-Output "All $($catalog.ValidValues.Count) demos passed ($Configuration)."
