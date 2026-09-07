# Phase 7 — Homing / reference establishment

> Historical phase report. Behavior and test counts below describe Phase 7 only.
> See [Phase 8](phase-8.md) and [protocol](protocol.md) for the current host baseline.

Phase 7 adds one-stage homing to the existing single-axis FreeRTOS controller.
The selected post-home positive departure allowance is implemented. No target
port, advanced homing, commit or push is part of this phase.

## Architecture and behavior

The portable `control/homing.c/.h` module owns seek, completion and abort results.
The existing 100 Hz Motion task calls it through the safety runtime adapter;
the existing 50 Hz Safety task retains fault and watchdog authority. No task,
queue or dynamic allocation was added. The motor's full safety gate remains
authoritative, with a directional guard for post-home negative output.

HOME is accepted only from healthy IDLE, including the permitted parked post-home
state. It is rejected while MOVING, STOPPING, HOMING, FAULT or ESTOP. MOVE/MOVE_REL
and another HOME are rejected during HOMING; status/help remain available.

| Event | Transition / action |
| --- | --- |
| Accepted HOME | IDLE → HOMING; fixed PWM **-0.30**, without PID |
| Expected negative switch | HOMING → IDLE; PWM zero, logical reference zero |
| Deadline at **40,000 ms** | HOMING → FAULT; retained HOMING_TIMEOUT, PWM zero |
| STOP | HOMING → STOPPING → IDLE after coast-to-rest; no new reference |
| Physical or software ESTOP | HOMING → ESTOP; drive removed, no successful home |
| Positive limit or other safety fault | HOMING → FAULT; drive removed, retained reason |

Elapsed time uses unsigned logical milliseconds, including wraparound. Timeout
wins if contact first appears exactly at the deadline. RESET is required after
FAULT/ESTOP and retains Phase 6 input/health/recovery checks. Neither RESET nor
releasing an input automatically resumes homing; a new HOME is required.

At successful contact, the existing `encoder_set_reference(0)` API establishes
logical zero without moving the plant or rewriting raw mechanical counts. Target,
PID integral/derivative state, output and completion dwell are reset. Encoder
velocity history is invalidated, making the first subsequent sample zero velocity
and preventing a reference-change spike. An interrupted attempt preserves the
existing encoder offset and reports STOPPED, ESTOPPED, FAULTED or TIMEOUT.

After success, the negative switch may remain active with zero output and the
full gate closed. Only an explicit MOVE/MOVE_REL targeting a greater logical
position may open the gate for positive departure. Negative output remains
blocked at the driver, including stale writes. The allowance expires on observed
switch release; later unexpected negative-limit activation faults normally.
Positive-limit activation always faults during HOMING. Fault/ESTOP also revoke
the allowance. RESET does not bypass an active negative switch.

The unchanged no-motion detector also supervises HOMING: commanded magnitude
at least 0.30 with encoder speed at most 25 counts/s for 500 ms faults under
Phase 6 rules. A stalled plant or frozen encoder therefore faults before the
40-second timeout. The detector does not claim to distinguish those causes.

## Measured simulation results

Times below are deterministic simulated time, not Windows execution duration.
Successful rows ended IDLE with logical position/target zero and PWM zero.

| Initial logical / raw count | Physical minimum | HOME elapsed | Final logical / raw count | Reference offset |
| --- | ---: | ---: | --- | ---: |
| 500 / 500 | 0 | 3590 ms | 0 / 0 | 0 |
| 1500 / 1500 | 0 | 10250 ms | 0 / 0 | 0 |
| 4000 / 4000 | 0 | 26920 ms | 0 / 0 | 0 |
| 7777 / 1600 | 100 | 10250 ms | 0 / 100 | -100 |

Already-active, unlatched IDLE contact completed without drive at 0 ms, including
a repeated HOME. The completion counter reached two. HOME after MOVE ending at
3001 took 20260 ms; HOME after a subsequent MOVE ending at 501 took 3590 ms.

MOVE after HOME from logical 0 to 500 completed at 501 (error/overshoot 1 count),
with completion at 3100 ms and measured settling at 3630 ms. The deterministic
trace hash was `f97123a66ac00184`. With physical minimum 100, logical 501 corresponded
to physical 601, demonstrating that the reference changes coordinates.

The timeout fixture moves the physical negative sensor/hard stop to -10000 so
motion continues without triggering no-motion first. At exactly 40000 ms it
reported TIMEOUT / FAULT / HOMING_TIMEOUT, PWM zero, logical/raw -4463 and unchanged
offset zero. Restoring the normal plant, RESET, a new HOME and MOVE succeeded.
The firmware timeout was not shortened for testing.

STOP reported STOPPED at 1080 ms, removed drive and coasted before returning to
IDLE. Physical/software ESTOP, positive limit, watchdog expiry, stall and frozen
encoder all aborted without false success. Valid recovery followed by HOME and
MOVE succeeded. The stall and frozen-encoder cases faulted at 1590 ms in their
injection schedules. Frozen counts stayed at 1376 while physical position reached
1298.786; the actual reference offset remained zero. Read-only host diagnostics
report published raw counts and reference offset separately from physical motion.

## Validation

Baseline Debug and Release each passed 75/75 before completing this work.

| Final command / check | Result |
| --- | --- |
| `./scripts/test.ps1` | **98/98 passed**, 103.63 s |
| `./scripts/test.ps1 -Configuration Release` | **98/98 passed**, 103.03 s |
| HAL isolation, both configurations | Passed |
| Deterministic control replay, both configurations | Passed |
| Deterministic safety replay, both configurations | Passed |
| Deterministic homing replay, both configurations | Passed |
| Compiler warnings | **0**, warnings treated as errors |

All **18 Debug demo scenarios passed**, including the literal default invocation
`./scripts/demo.ps1` (peripherals): nominal, peripherals, burst, rx-overflow,
diagnostic-loss, uart-long, uart-tx-failure, plant-open-loop, plant-limits,
closed-loop, estop, stall, encoder-failure, watchdog, limit-fault, homing,
homing-estop and homing-timeout. Each exited successfully with its expected
`PHASE*_OK` marker. Output is saved under `build/phase7-demo-<scenario>.log`.
The homing demo shows contact, reference establishment and positive departure;
the interruption/timeout demos also demonstrate explicit recovery and a new HOME.

Seven new unit cases cover reference/PID/velocity reset, already-active contact,
deadline/wrap, abort, state admission, safety and departure gates. Fifteen new
real-task scenarios cover `homing`, `homing-500`, `homing-4000`, `homing-reference`,
`homing-already`, `homing-after-move`, `homing-departure`, `homing-stop`,
`homing-estop`, `homing-software-estop`, `homing-positive-limit`, `homing-watchdog`,
`homing-stall`, `homing-encoder-failure` and `homing-timeout`. One replay test runs
five homing scenarios twice in separate processes and compares full quiet output.
These add 23 cases to the original 75.

Original control, E-stop, limit, no-motion, watchdog, RESET and replay coverage
remains. The finite manual nominal/UART diagnostic scripts already contained HOME;
their final snapshot now correctly expects HOMING at the 1200 ms cutoff instead
of treating HOME as unimplemented. Cooperative shutdown still verifies zero
motor output. Queue/parser/heartbeat/UART assertions were retained. The ordinary
four-move control sequence retains its previous metrics and trace hashes.

## Files

Created:

- `firmware/control/homing.c`, `firmware/control/homing.h`
- `tests/test_homing.c`, `cmake/CheckHomingReplay.cmake`
- `docs/homing.md`, `docs/phase-7.md`

Modified:

- `CMakeLists.txt`, `scripts/demo.ps1`
- `firmware/app/application.c`, `firmware/app/application.h`
- `firmware/control/motion_controller.h`, `firmware/drivers/motor.h`
- `firmware/platform/host/motor_host.c`, `firmware/platform/host/encoder_host.c`, `firmware/platform/host/encoder_host.h`, `firmware/platform/host/telemetry_host.c`
- `firmware/rtos/runtime_internal.h`, `firmware/rtos/safety_runtime.c`, `firmware/tasks/motion_task.c`
- `firmware/safety/fault_manager.c`, `firmware/safety/fault_manager.h`, `firmware/safety/safety_manager.c`, `firmware/safety/safety_manager.h`
- `simulator/main.c`, `simulator/closed_loop_main.c`, `tests/test_peripherals.c`
- `README.md`, `docs/architecture.md`, `docs/control.md`, `docs/safety.md`, `docs/simulation.md`, `docs/real-time-design.md`

The PID gains/equations, plant dynamics and vendored FreeRTOS sources were not changed.

## Scope and remaining work

An already latched negative-limit FAULT cannot be bypassed with HOME. A cold start
at an unrecognized active switch can therefore fault before HOME is processed;
service must clear the input and satisfy RESET. The already-active scenario
tests contact while still unlatched IDLE, and repeated HOME at a recognized home.
This preserves the selected narrow exception to Phase 6 safety semantics.

There is no debounce, backoff/re-approach, index capture or repeatability model.
The ideal hard stop removes outward velocity; hardware behavior is not validated.
MOVE still does not require prior HOME. Timeout RESET acknowledges the failed
attempt under current input/health checks; it cannot prove an unseen sensor repaired.
Native Windows results do not establish MCU deadlines or certified machine safety.

Future separately authorized work could implement the target port, hardware
validation or a richer homing policy. None was started in Phase 7.

| Required status | Result |
| --- | --- |
| Homing implemented | YES |
| Encoder reference established from simulated limit | YES |
| Post-home positive departure allowance implemented | YES |
| Full E-stop safety still functional | YES |
| No-motion safety still functional | YES |
| Watchdog supervision still functional | YES |
| Physical STM32 used | NO |
| Physical motor/encoder used | NO |
| ARM Cortex-M build | NO |
| Renode validation | NO |

See [homing design](homing.md) for detailed state, command and reference semantics.
