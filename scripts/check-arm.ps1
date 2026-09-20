param([Parameter(Mandatory=$true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$elf = Join-Path $BuildDirectory 'motion_controller_stm32.elf'
$compile = Get-Content (Join-Path $BuildDirectory 'compile_commands.json') -Raw
if ($compile -match 'platform[/\\]+host|simulator[/\\]|MSVC-MingW|ThirdParty[/\\]+GCC[/\\]+Posix|heap_[1-5]') { throw 'ARM source isolation failed.' }
if ($compile -notmatch 'ARM_CM4F' -or $compile -notmatch 'mfloat-abi=hard') { throw 'ARM port/ABI missing.' }
$header = & arm-none-eabi-readelf -h -A -S $elf
if ($LASTEXITCODE -ne 0) { throw 'readelf failed.' }
$header | Set-Content (Join-Path $BuildDirectory 'readelf.txt')
if (($header -join "`n") -notmatch 'Machine:\s+ARM' -or ($header -join "`n") -notmatch 'Tag_ABI_VFP_args:\s+VFP registers') { throw 'ELF is not ARM hard-float.' }
$symbols = & arm-none-eabi-nm --defined-only $elf
if ($LASTEXITCODE -ne 0) { throw 'nm failed.' }
$symbols | Set-Content (Join-Path $BuildDirectory 'symbols.txt')
foreach($symbol in @('main','vTaskStartScheduler','Reset_Handler','HardFault_Handler','g_pfnVectors','vPortSVCHandler','xPortPendSVHandler','xPortSysTickHandler','USART2_IRQHandler','motor_set_output','encoder_get_count','watchdog_refresh','motion_task','safety_task','comms_task','telemetry_task')) {
    if (($symbols -join "`n") -notmatch ('(?m)\s'+[regex]::Escape($symbol)+'$')) { throw "Required ARM symbol missing: $symbol" }
}
if (($symbols -join "`n") -match '(?m)\s(host_\w+|plant_\w+|[a-z_]*printf|malloc|calloc|realloc|free|_sbrk|_write|initialise_monitor_handles|vApplicationTickHook)$') { throw 'Forbidden host/heap/stdio symbol in ARM ELF.' }
$undefined = & arm-none-eabi-nm -u $elf
if ($LASTEXITCODE -ne 0 -or $undefined) { throw "Unresolved ARM symbols: $undefined" }
& arm-none-eabi-objdump -d $elf | Set-Content (Join-Path $BuildDirectory 'disassembly.txt')
if ($LASTEXITCODE -ne 0) { throw 'objdump failed.' }
& arm-none-eabi-size -A $elf | Set-Content (Join-Path $BuildDirectory 'sections.txt')
if ($LASTEXITCODE -ne 0) { throw 'size failed.' }
$bytes = [IO.File]::ReadAllBytes((Join-Path $BuildDirectory 'motion_controller_stm32.bin'))
if ([BitConverter]::ToUInt32($bytes,0) -ne 0x20020000) { throw 'Wrong initial MSP.' }
$reset = [BitConverter]::ToUInt32($bytes,4)
if (($reset -band 1) -eq 0 -or $reset -lt 0x08000000 -or $reset -ge 0x08100000) { throw 'Invalid Thumb reset vector.' }
foreach($entry in @(@(1,'Reset_Handler'),@(3,'HardFault_Handler'),@(11,'vPortSVCHandler'),@(14,'xPortPendSVHandler'),@(15,'xPortSysTickHandler'),@(54,'USART2_IRQHandler'))) {
    $line = @($symbols | Where-Object { $_ -match ('\s'+$entry[1]+'$') })
    $address = [Convert]::ToUInt32(($line[0] -split '\s+')[0],16) -bor 1
    if ([BitConverter]::ToUInt32($bytes,4*$entry[0]) -ne $address) { throw "Incorrect vector binding: $($entry[1])" }
}
$map = Get-Content (Join-Path $BuildDirectory 'motion_controller_stm32.map') -Raw
if ($map -match 'platform[/\\]+host|simulator[/\\]|MSVC-MingW|libwinmm|pthread|librdimon|libnosys') { throw 'ARM link isolation failed.' }
Write-Host 'ARM audit passed: Cortex-M4F hard-float, official port, required tasks/drivers, no host/heap/stdio, bounded memory layout.'
