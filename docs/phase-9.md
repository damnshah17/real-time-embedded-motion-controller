# Phase 9 — ARM Cortex-M firmware target

Phase 9 is complete: the existing portable motion controller builds as a genuine
STM32F407VG firmware image, and the host validation remains passing. No target
execution, physical hardware, commit or push was performed. Work stops here;
Phase 10 execution validation has not begun.

## Target and implementation

Selected **STM32F407VGT6 / STM32F4 Discovery family**, ARMv7E-M Cortex-M4F,
FPv4-SP-D16, 1 MiB flash and 128 KiB main SRAM. The additional 64 KiB CCM and
4 KiB backup SRAM are unused. F407 provides straightforward UART/timers, an
official FreeRTOS M4F port and existing Renode F4/Discovery definitions. F103 was
considered as a simpler Renode-supported alternative, but lacks the M4F hardware
floating-point/context path. Selection and primary references were recorded before
substantial implementation in [embedded-build.md](embedded-build.md).

The installed **Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)** is on PATH:
GCC **15.2.1 20251203**, binutils **2.45.1.20251203**. CPU flags are
`-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`. Release uses `-O3`,
Debug `-g`; no fast-math. PID single-precision arithmetic compiles to M4F
instructions; double calculations use libgcc helpers. FreeRTOS **V11.2.0
GCC/ARM_CM4F** handles lazy floating-point context stacking. Host builds retain
their existing Windows MSVC-MingW port; no host-port source was modified.

Target FreeRTOS configuration retains static tasks/queues, priorities and 100 Hz
tick, changes CPU clock to 16 MHz, disables the host tick hook, enables stack
overflow checking, and adds four-bit NVIC configuration. Priority grouping 3,
kernel priority 15, max-syscall threshold 5 and USART2 priority 6 are explicit.
No heap implementation is linked. Four application stacks and idle each reserve
1024 words; actual high-water usage has not been measured.

Reset establishes the actual MCU startup path: initial MSP, `.data` copy, `.bss`
zero, SystemInit, main, platform initialization, static runtime/tasks, scheduler.
Motion retains ownership of application startup. HSI 16 MHz supplies all buses
without a PLL. CP10/CP11 and VTOR are configured. The 98-entry vector table has
direct SVC/PendSV/SysTick port bindings and USART2 IRQ38. Fatal handlers disable
PWM through registers and stop watchdog refresh. The linker places flash at
`0x08000000`, main SRAM at `0x20000000`, and reserves MSP `0x2001F000..0x20020000`.
It rejects static RAM overlap and incorrect vector count.

| Peripheral | Compiled implementation | Execution status |
| --- | --- | --- |
| UART | USART2, PA2/PA3 AF7, 115200 8N1; bounded TX; RX IRQ to static frame queue and FromISR notification | Unvalidated |
| GPIO | Configurable PC1 E-stop, PC2 negative limit, PC3 positive limit; coherent active-high snapshot | Unvalidated |
| Time | SysTick/FreeRTOS 100 Hz; 10 ms resolution | Unvalidated |
| Motor/PWM | TIM3 CH1 PA6 AF2, 20 kHz, PC0 direction; zero/inhibited startup and preserved safety gates | Unvalidated |
| Encoder | TIM2 PA15/PB3 AF1, quadrature, real 32-bit CNT and logical reference offset | Unvalidated; no fake counts |
| Watchdog | IWDG /256, reload 124, nominal 1000 ms; Safety-qualified refresh | Reset unvalidated |

Portable Motion/Safety/Comms/Telemetry rates and behavior remain unchanged.
Target telemetry preserves protocol fields and ACK stages, using a bounded
formatter instead of printf. It uses six significant digits in scientific notation.
Detailed pin, clock, error, timeout and safety limitations are in
[stm32-port.md](stm32-port.md).

## ARM build and structural evidence

`scripts/build-arm.ps1` produces these Release artifacts:

- `build/arm-Release/motion_controller_stm32.elf`
- `build/arm-Release/motion_controller_stm32.bin`
- `build/arm-Release/motion_controller_stm32.map`

Readelf reports **ELF32, little-endian, EXEC, ARM, EABI5 hard-float**;
entry `0x0800275D`; CPU architecture **v7E-M**, Microcontroller, Thumb-2,
VFPv4-D16, SP-only hard FP and VFP-register argument ABI. Disassembly contains
`vmul.f32`, `vadd.f32`, `vdiv.f32` and the official port exception/context paths.

The audit finds main, scheduler, all four tasks, Reset/HardFault, SVC/PendSV/
SysTick, USART2, motor, encoder and watchdog symbols. Initial MSP and required
vector entries match the linked symbols. There are no undefined symbols.
Compile metadata and map exclude host platform, Windows/POSIX ports, simulator,
plant and tests. Symbol checks find no malloc/free, printf, semihosting or syscall
stubs. Negative fixtures correctly reject a host source, wrong PendSV vector and
Windows library. These are structural checks, not proof of boot or peripherals.

Actual `arm-none-eabi-size` results:

| Configuration | text | data | bss reported by size | total / hex |
| --- | ---: | ---: | ---: | --- |
| Release | 34372 | 4 | 30300 | 64676 / `0xFCA4` |
| Debug | 36456 | 4 | 30308 | 66768 / `0x104D0` |

Release flash load is **34376 bytes** (text+data; BIN length), **3.2784%** of
1 MiB. Actual `.bss` is 26204 bytes; GNU size includes the separate 4096-byte
NOLOAD MSP reservation in its bss column. Static data+bss is **26208 bytes**,
**19.9951%** of main SRAM. Including reserved MSP, RAM allocation is **30304 bytes**,
**23.1201%**, leaving 100768 bytes unallocated. Runtime stack headroom is unknown.

Section breakdown: vector 392, code/constants 33972, unwind 8, initialized data 4,
bss 26204, MSP reservation 4096 bytes. Major static contributors are four task
stacks 16384, idle stack 4096, diagnostic storage 3200 and UART RX storage 1088 bytes.
The map includes application/control/safety and actual FreeRTOS task/queue/port
objects; compiler arithmetic helpers support existing doubles and formatting.

Fresh configure/build/link succeeded in `build/arm-phase9-final`. A clean rebuild
in the main Release directory reproduced both ELF and BIN byte-for-byte; a fresh
directory reproduced the BIN. ELF debug metadata can contain build paths, so
cross-directory ELF byte equality is not required. Debug also builds and audits.

Release SHA256:

```text
ELF B2AF2FF295A746A192EEC7EFCF6E4F8E9AAE1E1CA06A8C6B76D1F9136739FB0B
BIN B6AD738DFCD09C16AF6495D7C4A285E3466F481E40580FDA39FAD039C3EA3B8E
```

Final clean ARM compiler/linker warnings: **0**. The transient repeated-configure
unused-toolchain warning was corrected. Host compilation remains warnings-as-errors
with no warnings in the recorded builds. The new extreme-float telemetry test
exposed a displayed-digit rounding error; double formatter intermediates corrected
it before the final clean builds and regressions.

## Host regression

| Check | Actual result |
| --- | --- |
| Before edits, Debug | 101/101, 109.83 s |
| Before edits, Release | 101/101, 110.38 s |
| Final Debug | **103/103**, 106.60 s |
| Final Release | **103/103**, 105.67 s |
| All public demos | **18/18 Debug**, default demo included |
| HAL isolation and rejection fixtures | Passed in both full suites |
| Protocol and control/safety/homing replays | Passed in both full suites |
| Embedded formatter host checks | Two added tests: boundaries/extremes and telemetry snapshot/output |
| Metrics repeatability | Two byte-identical Debug JSON files |
| Comparison to saved Phase 8 metrics | Configuration and measured scenario payloads unchanged |

Final metrics SHA256 is
`EBCBEF6393F80AC25BB5942D17B7B1A510A3BAFAAF6C8188C38C266DC9491F09`.
The inventory changed from 101 to 103 tests; no control/safety/homing parameters
or results changed. Validation ran serially. Logs and reports are under ignored
`build/phase9-*`; they are local evidence, not committed artifacts.

ARM CI is deferred until a pinned toolchain installation/checksum job is adopted.
Existing host CI is unchanged and now discovers the two additional tests. Remote
CI was not run.

## File inventory

Created:

- `cmake/STM32F407.cmake`, `cmake/toolchains/arm-none-eabi.cmake`
- `scripts/build-arm.ps1`, `scripts/check-arm.ps1`
- `firmware/platform/stm32/`: `FreeRTOSConfig.h`, `board.h`, `startup.S`,
  `stm32f407vg.ld`, `system.c`, `main.c`, `platform.c`, `motor.c`, `encoder.c`,
  `peripherals.c`, `uart.c`, `telemetry.c`, `tiny_format.c`, `tiny_format.h`
- `tests/test_stm32_format.c`, `tests/test_stm32_telemetry.c`
- `third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c`, `portmacro.h`
- `third_party/CMSIS/`: five selected core headers and license;
  `third_party/STM32F4/`: two device/system headers, original startup and license;
  `third_party/arm-support.sha256`
- `docs/embedded-build.md`, `docs/phase-9.md`

Modified:

- `CMakeLists.txt`, `.gitattributes`
- `firmware/drivers/motor.h`, `firmware/drivers/watchdog.h`, `firmware/rtos/runtime.h`
  (contract comments only)
- `third_party/FreeRTOS-Kernel.sha256`, `third_party/README.md`
- `README.md`, `docs/architecture.md`, `docs/real-time-design.md`,
  `docs/stm32-port.md`, `docs/phase-8.md` (historical banner)

## Phase 10 readiness and claim boundary

The ELF and source/platform separation are ready for a scoped Renode boot attempt.
Renode's generic F4 definitions use larger memory and different clock defaults;
Phase 10 must configure the exact MCU limits and 16 MHz clock, then prove startup,
FPU exceptions/context, scheduler, UART RX/TX and command execution. GPIO faults,
timer encoder/PWM modeling and watchdog reset also need execution checks. Real
electrical behavior, sensor wiring, bridge sequencing, oscillator tolerances,
latency, stack margins and hardware safety require further validation. No emulator
execution or `.resc` scenario work was done here.

| Required declaration | Result |
| --- | --- |
| Genuine ARM Cortex-M ELF built | **YES** |
| Correct Cortex-M FreeRTOS port used | **YES** |
| STM32 platform sources compiled | **YES** |
| Host simulator excluded from ARM image | **YES** |
| Host validation still passing | **YES** |
| Physical STM32 used | **NO** |
| Physical motor/encoder used | **NO** |
| Renode execution performed | **NO** |
| Hardware timing validated | **NO** |

No commit or push. Phase 9 ends at verified cross-compilation and host regression.
