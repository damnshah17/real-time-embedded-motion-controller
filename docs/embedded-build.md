# STM32F407 firmware build

Phase 9 selects **STM32F407VGT6**, Cortex-M4F, 1 MiB flash, 128 KiB main SRAM,
64 KiB CCM (unused), on the STM32F4 Discovery family. Selection precedes target
implementation. STM32F103 is a plausible Renode-supported alternative, but the
F407 supplies the M4F floating-point context needed by this control workload.

Primary references: [ST device datasheet](https://www.st.com/resource/en/datasheet/stm32f407vg.pdf),
[ST device headers v2.6.10](https://github.com/STMicroelectronics/cmsis-device-f4/tree/v2.6.10),
[CMSIS 5.9.0](https://github.com/ARM-software/CMSIS_5/tree/5.9.0),
[FreeRTOS V11.2.0 CM4F port](https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.2.0/portable/GCC/ARM_CM4F),
[Renode Discovery board](https://github.com/renode/renode/blob/master/platforms/boards/stm32f4_discovery.repl),
[Renode F4 platform](https://github.com/renode/renode/blob/master/platforms/cpus/stm32f4.repl),
[F103 alternative](https://github.com/renode/renode/blob/master/platforms/cpus/stm32f103.repl).

Renode's generic F4 platform has UART/GPIO/timer/IWDG models, but its flash/SRAM
sizes and clock defaults differ from this exact MCU/build. Phase 10 must constrain
memory and configure clocks, and verify FPU, encoder and PWM model behavior.
No Renode execution or physical hardware validation is claimed in Phase 9.

## Tools and commands

Validated on Windows with Arm GNU Toolchain **15.2.Rel1 (Build arm-15.86)**,
GCC **15.2.1 20251203**, GNU binutils **2.45.1.20251203**, CMake 4.4.0 and Ninja 1.13.2.
The installed bin directory is
`C:\Program Files\Arm\GNU Toolchain mingw-w64-x86_64-arm-none-eabi\bin` and is on PATH.
No IDE or network download is required for a checkout build; dependencies are
pinned, licensed and hash checked. See [provenance](../third_party/README.md).

```powershell
./scripts/build-arm.ps1
./scripts/build-arm.ps1 -Configuration Debug
./scripts/build-arm.ps1 -BuildDirectory build/arm-fresh
./scripts/build-arm.ps1 -Clean
./scripts/check-arm.ps1 -BuildDirectory build/arm-Release
```

The script checks required programs, configures Ninja with
`cmake/toolchains/arm-none-eabi.cmake` and `MOTION_PLATFORM=stm32f407`, builds,
generates a binary, prints size and runs the structural audit. `-Clean` invokes
Ninja clean only in the specified build directory. Keep host/ARM directories
separate. `MOTION_PLATFORM=host` is the default; host builds reject cross compilers.

Release uses `-O3 -DNDEBUG`; Debug uses `-g`. Both use C11 and
`-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`, function/data sections,
`-Wall -Wextra -Wpedantic -Werror`, and linker section garbage collection.
There is no fast-math or fixed-point PID rewrite. FreeRTOS assertions remain
enabled in Release. PID single-precision operations use M4F hardware; existing
double intermediates and target decimal formatting use libgcc software helpers.
CP10/CP11 are enabled before application floating-point work. The official
CM4F port enables lazy FPU stacking and saves/restores additional FP context.

## Startup, memory and RTOS

The initial vector loads MSP=`0x20020000`. Reset masks interrupts, copies `.data`,
zeros `.bss`, calls SystemInit, then main. The 98-entry vector table starts at
`0x08000000`, aligned to 512 bytes. Reserved entries stay zero; SVC/PendSV/SysTick
bind directly to the port, USART2 to IRQ38, HardFault to its fatal handler, and
unused exceptions to Default_Handler. This C11 image uses no C++ constructors.

SystemInit selects HSI 16 MHz, AHB/APB1/APB2 divisors 1, no PLL, and programs VTOR,
FPU access and priority grouping. TIM2/TIM3 peripheral clocks are 16 MHz.
USART2 BRR 139 gives approximately 115108 baud for requested 115200 (nominal HSI).
No oscillator accuracy, interrupt latency or actual loop timing has been measured.

| Region | Origin | Capacity/use |
| --- | --- | --- |
| Flash | `0x08000000` | 1 MiB; vectors, code, constants, unwind table, `.data` load image |
| Main SRAM | `0x20000000` | 128 KiB; `.data`, `.bss`, reserved top 4 KiB MSP stack |
| CCM | `0x10000000` | 64 KiB, deliberately unused |
| Backup SRAM | `0x40024000` | 4 KiB, unused |

The linker asserts vector size and static RAM not overlapping MSP reservation.
Four task stacks plus idle use 1024 32-bit words each (20 KiB total);
these are part of `.bss`, not additional heap. Static command queue 8, diagnostics 32
and RX frames 16 retain bounded capacities. Dynamic allocation is disabled, kernel
static idle storage enabled, software timers disabled, stack overflow checking 2.
No `_sbrk`, `_write`, semihosting or malloc stubs are needed. Used libc memory/
string operations and libgcc arithmetic helpers survive garbage collection.

Main initializes motor to zero/inhibited before other peripherals, then initializes
the shared runtime and four static tasks. Motion performs the existing application
startup state machine. Main enables UART NVIC routing while PRIMASK remains set;
the official port unmasks interrupts when starting the first task. Failure or a
returned scheduler enters the register-only fatal path.

Host and target retain five priorities: Safety 4, Motion 3, Comms 2, Telemetry 1,
idle 0. The 100 Hz tick supports Motion 100 Hz, Safety 50 Hz, Telemetry 10 Hz and
Comms 50 ms health timeout. Target configuration differs from host in CPU clock,
disabled host tick hook, overflow checking, and Cortex-M interrupt settings.

CMSIS declares four priority bits. Grouping 3 assigns all four to preemption.
Kernel PendSV/SysTick priority 15 (`0xF0`); max-syscall threshold 5 (`0x50`);
USART2 priority 6 (`0x60`) permits its FromISR calls. Higher-urgency priorities 0–4
must not use FreeRTOS APIs. The UART ISR pends a switch after a delivered frame;
short register/driver operations preserve PRIMASK across nested callers.

## Artifact inspection and limits

Generated files in each build directory:

- `motion_controller_stm32.elf`, `.bin`, `.map`;
- `compile_commands.json`, `readelf.txt`, `symbols.txt`, `disassembly.txt`, `sections.txt`.

The audit verifies ARM hard-float metadata, required application/port/driver symbols,
no unresolved references, initial MSP and reset/SVC/PendSV/SysTick/USART2 vector
addresses, and host/simulator exclusion from source/link configuration. Inspect
`arm-none-eabi-readelf -h -A -S`, `arm-none-eabi-nm`, `arm-none-eabi-objdump -d`,
and `arm-none-eabi-size -A` directly for detailed evidence. The report records
actual sizes and clean-build hashes; binary size excludes debug metadata and SRAM.

The bounded formatter avoids printf and heap use, rejects overflow, preserves
STATUS fields and uses six significant decimal digits in scientific notation.
Host tests exercise extremes, output limits and STATUS snapshot coherence.
The two telemetry adapters should be updated together if protocol fields change.
Exact formatted-byte equality across platforms is not promised.

ARM CI is deferred: existing Windows CI remains host-only, and this phase does
not add a network-dependent compiler installation. A future build-only job should
pin the toolchain archive and checksum before adoption. Remote CI has not run.

Phase 10 can begin with the generated ELF, explicit F407 memory limits and 16 MHz
clock settings. It still needs to prove reset-to-scheduler boot, UART IRQ/commands,
task scheduling, GPIO faults, watchdog behavior, FPU support and timer model
compatibility. Host plant proofs do not establish MCU execution or physical motor
behavior. No Renode scripts or execution were introduced in Phase 9.
