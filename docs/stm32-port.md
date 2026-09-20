# STM32F407 target and platform boundaries — Phase 9

The STM32F407VGT6 target **cross-compiles and links**. UART, GPIO, PWM, encoder,
tick and IWDG register implementations are compiled; execution is unvalidated.
See [build instructions](embedded-build.md) and [results](phase-9.md).
The host remains the functional motor-physics and safety-policy test environment.

| Contract | Host implementation | STM32F407 compiled implementation |
| --- | --- | --- |
| UART | Scripted 16-frame static RX queue, bounded TX sink | USART2 PA2/PA3 AF7, 115200 8N1, RX IRQ38, 16-frame static queue; bounded polling TX |
| GPIO | Injected logical inputs | PC1 E-stop, PC2 negative limit, PC3 positive limit; active-high pulldown, one IDR snapshot |
| Motor | Signed duty model and safety gates | TIM3 CH1 PA6 AF2, 20 kHz PWM; PC0 direction (high positive), zero at initialization |
| Encoder | Injected 32-bit count and reference | TIM2 CH1 PA15 / CH2 PB3 AF1, 32-bit quadrature counter, modulo logical reference |
| Time | Simulated milliseconds or host kernel ticks | FreeRTOS SysTick, 100 Hz, 10 ms resolution, uint32 millisecond wrap |
| Watchdog | Refresh observations, no autonomous reset | IWDG, nominal 32 kHz LSI, /256 prescaler, reload 124 for nominal 1000 ms |
| Telemetry | Bounded snprintf | Bounded project formatter; six significant digits in scientific notation for nonzero floats |

Safety input port, clock enable, pin numbers, polarity and pulls are defined in
`firmware/platform/stm32/board.h`. They describe an external test harness, not
Discovery onboard buttons. PA15/PB3 use JTAG/SWO pins; SWD PA13/PA14 remains
available, but full JTAG/SWO is unavailable with this encoder mapping. Board
wiring and peripheral pin sharing need review before physical connection.

PWM updates disable the channel and zero CCR before direction changes. Both
inhibit assertion and release remove output without replaying requested duty.
The narrow post-home negative-drive guard remains in force. Duty saturates to
[-1,1], nonfinite input removes output, and hardware resolution is 1/800. There
is no bridge dead-time, complementary PWM, current sensing or hardware E-stop
circuit. Encoder reads TIM2 CNT; no synthetic target counts exist. Its reference
offset changes the logical count without rewriting the counter.

USART2 ISR reads SR/DR, handles at most one byte per entry, frames CR/LF lines,
and posts frames with `xQueueSendFromISR` plus a task notification. CRLF produces
one frame. Long frames saturate at 64 bytes and are rejected by the existing
parser. Hardware errors discard the current line through the next delimiter.
Full RX queue drops the newest frame. Inspectable counters record RX errors/drops
and TX timeouts; these transport counters are not added to portable STATUS.
No ISR performs command parsing, formatting or motion.

Only Telemetry transmits after startup. Each write accepts at most 256 bytes;
each TXE/TC wait has a finite 100000-iteration limit and remains preemptible.
Timeout can follow a partial write. This is bounded polling, not DMA, flow control
or guaranteed delivery. Command syntax, ACK stages, STATUS field names and coherent
snapshot semantics match the host. Numeric display style differs; target nonfinite
values display `invalid`.

IWDG accepts 8..32768 ms, rounds up to nominal 8 ms units and cannot be disabled
once started. Only qualified Safety refreshes it after startup. LSI tolerance
affects timeout; reset cause, actual reset and debug freeze remain unvalidated.
GPIO active levels do not detect an open wire, and encoder initialization does
not establish that a physical sensor is connected or healthy.

## Source and configuration separation

`cmake/STM32F407.cmake` compiles existing app/control/protocol/health/safety, task
and RTOS orchestration sources, with `firmware/platform/stm32` drivers. It selects
FreeRTOS V11.2.0 `portable/GCC/ARM_CM4F` and the target's separate configuration.
Host hooks/ports, peripheral models, tests and simulator sources are excluded.
Normal host targets and scripts remain available.

`scripts/check-arm.ps1` checks compile metadata, ELF attributes, expected symbols,
vector bindings, unresolved/forbidden symbols and the link map. It complements the
portable HAL text guard; neither substitutes for execution. No heap, semihosting,
empty syscall stubs or full printf implementation is linked.

The fatal path disables interrupts and PWM using direct registers, then waits
without feeding IWDG. It covers startup failure, assertions, stack overflow and
unexpected exceptions without diagnostics. Physical shutdown is not proven.
