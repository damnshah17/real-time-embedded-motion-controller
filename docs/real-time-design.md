# Real-time execution design — Phase 9

Host timing evidence remains simulation-only. The STM32F407 build preserves
these task periods at a 16 MHz HSI clock and 100 Hz SysTick, with M4F context
switching. UART TX polling runs in preemptible Telemetry; RX framing runs at
NVIC priority 6 and notifies Comms. Cortex-M timing, task stack watermarks, UART
throughput under load and watchdog reset remain unmeasured. Target stack
reservations and IRQ settings are in [embedded-build.md](embedded-build.md).

## Priorities and execution bounds

FreeRTOS V11.2.0 runs preemptively with five priority levels (0–4). Safety uses 4,
Motion 3, Communications 2 and Telemetry 1; idle uses 0. Safety is highest and now
evaluates GPIO, encoder response and critical health, with final motor inhibit
authority. Motion owns ordinary control state; the safety override and commands
are synchronized so Communications cannot race state handling.

Motion runs every 10 ms (100 Hz), Safety every 20 ms (50 Hz), Telemetry every 100 ms
(10 Hz). Communications waits for an input notification, with a 50 ms timeout so
it can report health while idle. Its traffic-driven heartbeat is not a fixed-rate
task. The kernel tick is 100 Hz. All these periods are integer tick multiples.

Periodic tasks use `xTaskDelayUntil`, not ordinary OS sleeps or relative waits that
accumulate work-time drift. If a release is already due, the periodic helper records
an overrun, yields one tick and reanchors after yielding. A regression test forces
this path and verifies the following normal period is not counted as another miss.
This is an observable scheduling policy, not a deadline guarantee.

Work is explicitly bounded: Motion receives at most two commands per cycle;
Communications handles four input frames per batch, then self-notifies/delays one
tick if more remain; Telemetry drains up to 32 records per cycle. Safety/Motion
have no waits on command input or logging. Their deliberate periodic waits allow
lower-priority tasks and idle to execute. Future unbounded computation in either
high-priority task could still starve the others and must not be introduced.

The simulator-only HostHarness runs at priority 1, wakes after 1200 ms of RTOS
time and coordinates test completion. It is not an extra firmware responsibility.

## Primitives and blocking

| Mechanism | Actual use | Blocking behavior |
| --- | --- | --- |
| Static command queue, 8 entries | Communications → Motion, copied typed data | Zero-wait send/receive; reject newest when full |
| Static host RX queue, 16 frames | Tick-context source → Communications HAL | FromISR send; zero-wait task receive; drop newest when full |
| Direct task notification | RX-ready wake-up for Communications | `ulTaskNotifyTake` waits at most 50 ms; count cleared on wake |
| Static diagnostic queue, 32 records | Application/tasks → Telemetry | Zero wait; count/drop newest when full |
| Critical sections | Coherent snapshots, health records and queue statistics | Short, bounded copies/counters; no formatting or external I/O |

A notification carries no payload because the input transport retains the data.
It is a lighter one-recipient readiness signal than adding a separate semaphore.
The host RX queue implements that transport today; a future UART ring/DMA buffer
can keep the same event/receive contract. Communications drains based on queue
contents, not notification count. The integration checks include actual ISR
signals; the kernel test verifies two notifications can be counted and cleared.

No application semaphore or mutex exists. There is no application mutex-induced
priority inversion to solve in the current design. Critical sections do defer
preemption briefly, so their lengths must remain bounded. The native host port
uses OS synchronization internally, and host stdio/OS services are outside RTOS
timing guarantees. This design does not claim priority inversion is impossible
inside the host runtime.

## Interrupt rules

The official Windows port's timer thread generates a simulated tick processed by
the port's interrupt dispatcher. `xTaskIncrementTick` invokes `vApplicationTickHook`,
which runs the registered host input callback. It releases up to 16 due frames with
`xQueueSendFromISR` and invokes `vTaskNotifyGiveFromISR` once when any were accepted.
No task context is disguised as an interrupt. The POSIX path uses its official
signal-based tick machinery; Linux execution is not locally validated.

The tick implementation checks the pending-yield flag after the callback returns.
These FromISR calls pass NULL for the woken-task pointer and rely on that enclosing
tick's context-switch decision. Calling the Windows `portYIELD_FROM_ISR` macro in
this void hook would be wrong because it returns a value. The separate STM32 UART
ISR pends a Cortex-M context switch after delivering a frame/notification. It
unconditionally requests the switch because the shared notification helper does
not expose the woken-task flag; this may cause an unnecessary scheduling decision.

Real Cortex-M ISR validation must also configure priority grouping and the
`configMAX_SYSCALL_INTERRUPT_PRIORITY` boundary: interrupts above the allowed
RTOS-call threshold cannot use kernel APIs. Phase 9 compiles grouping 3 (four
preemption bits), kernel priority 15, syscall threshold 5, USART2 priority 6.
Execution and latency remain untested. No ISR parses commands, computes PID,
formats telemetry or blocks; the UART ISR only frames bytes into a bounded queue.

## Shared state and health

Motion's application object is private. It publishes state and receipt information
under a short critical section. Each critical task updates only its own heartbeat
record through a synchronized function. Telemetry copies the aggregate under the
same mechanism before formatting it outside the protected section.

Each record has a counter, unsigned millisecond timestamp and `seen` flag. The flag
distinguishes a never-started task from a counter that wrapped to zero. Freshness
uses unsigned subtraction with a 150 ms threshold; the unit test covers wrap and
independent task ages. This assumes observations occur within one timestamp cycle
(about 49.7 days), as for normal periodic supervision. Safety now supervises all
three critical records every 20 ms. Only all-fresh records permit watchdog refresh.
After a 200 ms startup grace, missing health raises a retained WATCHDOG fault;
unhealthy records inhibit output even during grace. The host watchdog timeout is
1000 ms and remains observable without autonomous expiry/reset. This is Option A:
software supervision trips before expiry. A truly stopped supervisor requires a
future independent hardware watchdog; suppressing its heartbeat is not a CPU hang.

The queue high-water observation and send happen in one critical section, avoiding
an underestimate if Motion drains the queue between the two. Input statistics are
owned by the tick callback and copied with interrupts masked. Fixed storage and
copy-by-value messages avoid buffer-lifetime races.

## Logging and overload

Telemetry is the sole caller of normal UART output while scheduling is active.
Higher-priority tasks enqueue fixed-size diagnostic records. Formatting and stdio
never occur inside their critical sections. Console output may block Telemetry;
producers continue until the diagnostic queue fills, then count/drop new records.
The diagnostic-loss test verifies application startup, command flow and heartbeats
still work. Phase 3 additionally tests a UART sink that returns failure for every
write. Successful operation under returned I/O errors does not measure every
possible indefinitely blocked host console.

UART TX is limited to 256 bytes per call. Only the host UART sink touches stdout.
Bounded formatting stays in the host telemetry adapter and never runs in an ISR.
TX statistics count successful calls/bytes and rejected or failed calls. A physical
sink can fail after a partial write; success is not reported in that case and the
driver does not retry or roll back bytes. The Communications task now reads through
`uart_try_read_line`, backed by the same Phase 2 RX queue and readiness notification.
The tick-hook release of complete frames remains host scaffolding. Phase 9 supplies
a USART2 RX ISR, static frame queue and task notification for STM32. No physical
UART baud/framing timing has been validated.

Queue admission and command execution are separate results. A syntactically valid
MOVE receives `ACK QUEUED` on admission, then Motion's `ACK ACCEPTED` or a state/range
rejection. `MOTION COMPLETE` is emitted only after the completion dwell. ACK/ERR
diagnostics themselves can be lost under output overload. Guaranteed protocol
responses need a later bounded transport/flow-control policy.

## Allocation and timing limits

All application task/queue objects, stacks and buffers are statically allocated.
The kernel provides static idle storage, dynamic FreeRTOS allocation is disabled,
and no heap implementation is linked. The upstream Windows port allocates OS
threads, events and stacks; POSIX also uses internal malloc/free. Desktop native
stack usage is not Cortex-M stack evidence. No stack watermark claim is made.

The official Windows port pins its native task/interrupt threads to one CPU and
uses elevated native priorities; it requires a multicore host. Its simulated timer
and Windows scheduling do not deliver MCU hard-real-time guarantees. The configured
100 Hz tick is intentionally modest for host architecture development. Tick-domain
Motion gaps and overrun counts are reported; wall-clock period/jitter measurement,
1 kHz control tuning and physical timing validation remain future work.

## Peripheral concurrency and numerical semantics

The plain C host models accept access hooks installed before scheduling. In the
RTOS executable these map to task critical sections. Initialization is startup-only;
normal motor, encoder, GPIO and watchdog accesses are short protected operations.
Snapshot publication uses the existing runtime critical section. No new mutex or
semaphore was introduced. Calls from arbitrary native threads or ISRs are not
supported; simulated RX interrupt access continues through its queue's FromISR APIs.

UART I/O executes outside the peripheral access critical section. Timer-source and
UART adapter pointers are immutable during scheduling. The timer driver exposes
only modulo-2^32 milliseconds and explicit resolution: 1 ms manually advanced
logical time in unit tests, 10 ms RTOS-tick time in the threaded host. Unsigned elapsed
time handles one wrap; a full counter cycle or more cannot be distinguished.

The motor driver clamps finite out-of-range duty; NaN/Inf forces zero and returns
an error. Disable is not a latched emergency-stop interlock: a subsequent valid
driver command can set output again only if the independent safety gate permits it.
Motion now commands bounded PID output while
MOVING and zero output in IDLE/STOPPING. Encoder arithmetic uses unsigned wrap and explicit
signed conversion; it is not a physical quadrature decoder. GPIO inputs are logical
active values without electrical polarity, debounce or interrupt modeling. Safety
applies the implemented E-stop/limit policy to those inputs.

Primary sources: [official V11.2.0 Windows port](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/V11.2.0/portable/MSVC-MingW/port.c),
[port definitions](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/V11.2.0/portable/MSVC-MingW/portmacro.h),
[kernel tick and notification implementation](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/V11.2.0/tasks.c),
and the matching unmodified sources in `third_party/FreeRTOS-Kernel`.

## Preserved open-loop plant time

The new `motion_controller_plant` runner owns a fixed 1 ms numerical step. This is
1000 simulation updates per simulated second, not a 1 kHz RTOS task or a measured
host execution rate. There is no sleep, host clock sampling or scheduler in this
executable. The normal RTOS executable keeps its 100 Hz tick and four firmware
task priorities; Motion now executes PID. No fifth firmware task was introduced.

`plant_step` reads PWM once, computes the next state locally, then publishes raw
encoder/negative-limit/positive-limit values in a short host access transaction.
The outer transaction nests the existing driver hooks, so their established nesting
contract applies. No computation, allocation, formatted output or wait occurs under
that transaction. The model/configuration/snapshots belong exclusively to the runner;
only peripheral state uses access hooks. Concurrent model calls are unsupported.

The runner separately advances the manual timer by `step_ms` after each successful
plant update. Pure plant unit tests need no timer advancement or elapsed Windows
time. Reset clears the plant step counter and mechanics but does not rewind the
platform clock. Replaying the same configuration, initial state and per-step PWM
sequence uses the same floating-point operation order; tests compare state fields
exactly within one build. Cross-compiler/CPU bitwise identity is not promised.

Semi-implicit Euler with the default damping-step product 0.004 gives positive
unpowered decay factor 0.996. Configuration rejects a damping-step product above
1, non-finite/invalid values and unsafe representational ranges. Speed saturation
and hard travel clamps are explicit model constraints, not firmware fault behavior.
Plant diagnostics run at 2 Hz of logical time plus scenario transitions, outside
firmware telemetry. They can print rapidly in real time because the runner has no
pacing. No wall-clock jitter or MCU timing claim follows from these results.

## Closed-loop execution in Phase 5

Motion remains priority 3 at 100 Hz; Safety/Comms/Telemetry keep priorities 4/2/1
and their existing schedules. The normal RTOS runner computes dt from unsigned
platform-time differences and uses the preserved periodic overrun policy. Startup
stepping now runs only during BOOT/INITIALIZING. Motion then owns encoder sampling,
command execution, PID, completion and actuator writes.

The separate closed-loop runner selects external Motion releases before scheduling.
Its single host harness advances ten 1 ms plant steps and logical milliseconds,
notifies Motion, then waits for that task's completion notification before advancing
again. Motion never calls the plant and the harness never calls PID. The handshake
keeps plant/control order deterministic despite Windows preemption. A one-second
notification timeout detects a broken run; elapsed wall time never determines dt.
This demonstrates 100 Hz control in simulated time, not physical 100 Hz deadlines.

UART scenario input uses a bounded task-context submission and task notification,
then the real Comms parser/queue. The original FromISR tick path remains tested in
the normal runner. Motion processes at most two commands per release; PID work and
state are fixed-size. No application heap, mutex or new firmware task was added.
Ordinary PWM requests belong to Motion; final inhibit belongs to Safety. Plant
mechanics belong to the harness. Sensor
publication retains the short nested host critical-section hooks.

Safety uses a second release/completion handshake every 20 logical milliseconds,
before Motion at coincident releases. Comms is notified every 10 ms to make its
logical-time health observable. Telemetry alone remains scheduled by kernel ticks
and is not safety critical. The original RTOS tests still check normal task
periods, health, queues and simulated interrupts. Motion disables PWM before parking;
all tasks park before final output and hooks are detached after scheduler return.

Firmware telemetry uses the five-group [protocol schema](protocol.md), including
target, encoder velocity, error and requested/applied PWM. Demo traces are collected at 500 ms
logical intervals and printed after shutdown; normal firmware console output remains
Telemetry-owned. Metrics use every control sample, including two post-completion
seconds, not the sparse playback. See [control design](control.md) for equations,
quantization, completion rules and metric definitions.

## Phase 7 homing execution

Homing is a bounded branch of the existing Motion update, not a new task or PID
trajectory. Each 10 ms release checks GPIO, the 40000 ms deadline and fixed -0.30
duty. Safety remains at priority 4 / 20 ms with heartbeat, limit and no-response
supervision. Motion also reconciles safety when its home update sees any active
safety input, before establishing a reference. This supplements periodic observation;
it does not claim a measured hardware response bound.

Reference zeroing, estimator/PID reset, completion and post-home authorization occur
under the existing short control critical section. The motor's full gate and new
negative-direction guard share its nested driver critical sections. No mutex,
allocation, blocking logging or plant computation was added to these sections.
GPIO release revokes the departure allowance; the same task snapshots expose that
change. Normal Motion/Safety/Comms/Telemetry priorities and periods are unchanged.

Phase 8 STATUS captures a coherent runtime snapshot at Telemetry service time,
then formats five bounded lines outside synchronization. This does not promise
command-time state or new simultaneous device reads. The unchanged diagnostic
drain bound also limits STATUS/HELP service; overload can still drop records.
Compiled-configuration and scenario-metrics output is host-only and runs outside
the control loop. No task period, allocation or high-priority I/O was added.
