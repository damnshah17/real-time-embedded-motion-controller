# Phase 5 — PID / closed-loop motion control

> Historical phase report. Behavior and test counts below describe Phase 5 only.
> See [Phase 8](phase-8.md) and [protocol](protocol.md) for the current host baseline.

## Completed implementation

Before changes, the requested documentation, Motion task, command bus/parser,
application state, drivers, plant, runners, telemetry and tests were inspected.
Both baseline configurations passed **35/35**. Phase 5 preserves all those tests,
the FreeRTOS V11.2.0 kernel/port, four task priorities, bounded queues, notifications,
heartbeats, static RTOS allocation, peripheral abstractions and Phase 4 plant.

The real Motion task now executes portable encoder-feedback PID and motion state
logic. MOVE and MOVE_REL work, validate targets and complete with zero PWM after
stable arrival. STOP removes drive and waits for estimated rest. The host coordinates
plant/control stepping through notifications; it never calls PID itself. UART frames
still pass through the real Communications parser and typed command queue.

## Control implementation and configuration

| Requested item | Implemented behavior/value |
| --- | --- |
| PID module | Reusable `firmware/control/pid.c/.h`, no RTOS/platform/application dependency |
| PID equation | `error=target-measurement; I=clamp(I+Ki*error*dt); D=-(measurement-previous)/dt; PWM=clamp(Kp*error+I+Kd*D)` |
| Kp / Ki / Kd | **0.006 / 0.0002 / 0.0015** |
| Anti-windup | One strategy: clamp I contribution to **±0.005 PWM** |
| Derivative | On measurement; zero first derivative, seeded on new move; no filter |
| Control frequency | **100 Hz**, unchanged 10 ms Motion period |
| Plant frequency | **1000 Hz** logical time, ten 1 ms steps per control interval |
| Position | `int32_t` logical encoder counts; default **1 count = 1 axis unit** |
| Velocity | Signed modulo encoder delta / dt, first sample zero; no plant-velocity feedback |
| PWM limit | **±0.8** in PID; existing driver ±1 defensive bound retained |
| Target travel | **0..5000** logical counts, inclusive; relative addition uses int64 |
| Position tolerance | **±3 counts** |
| Dwell | **20 consecutive qualifying updates**, nominal 200 ms at 100 Hz |
| Completion velocity | Magnitude **≤25 counts/s** |
| Busy MOVE policy | Reject MOVING/STOPPING requests with **INVALID_STATE**, retain active target |
| Zero-distance move | Normal dwell with zero PWM; measured completion **190 ms** after acceptance sample |
| SPEED | Deferred; **NOT_IMPLEMENTED**, no fake velocity loop |
| STOP | Implemented: drive zero, MOVING -> STOPPING -> IDLE after low-velocity dwell; coasts |
| ESTOP / HOME / RESET | **NOT_IMPLEMENTED**; full safety/homing remain later work |

The portable motion controller keeps command semantics separate from PID. A new move
resets PID and dwell. Position tolerance and low estimated velocity must both hold
before completion; any excursion resets dwell. Queue admission, Motion acceptance
and eventual completion have distinct diagnostics. Invalid feedback/dt/arithmetic
disables output and cancels the move to IDLE with an error count/log; no retained
FAULT or encoder-timeout state machine was added.

The control configuration was selected manually using the existing simplified plant
and validated against every listed scenario. Initial gains passed without adjustment;
no autotuning or hardware identification is claimed. [Control design](control.md)
explains the gain rationale, anti-windup, numeric types, first-sample behavior,
encoder wrap assumptions, output limits and remaining quantization effects.

## RTOS and simulator ownership

The original periodic executable still runs Motion at 10 ms using its existing
periodic wait/recovery policy. Startup stepping runs only in BOOT/INITIALIZING so
it no longer overrides active motion output. The same Motion task can instead wait
for explicit cycle releases selected before scheduler startup.

In `motion_controller_closed_loop`, the single host harness advances ten 1 ms plant
steps, advances manual time, notifies Motion and waits for its completed-cycle
notification. Motion samples encoder, processes at most two commands, updates PID,
writes PWM and publishes its snapshot before acknowledging. No additional firmware
task, mutex, semaphore or application heap was introduced. The host harness replaces
the role of the previous finite test harness in this executable.

Commands enter the existing UART RX queue using task-context injection and a task
notification. Higher-priority Comms parses and queues them before Motion is released.
The original tick/FromISR path is preserved and remains separately tested. Sensor
access uses the existing short host critical-section hooks; numerical plant work
is outside them. Only Motion writes actuator commands during closed-loop execution.

The handshake fixes control/plant order, not host wall-clock timing. Safety and
Telemetry retain tick-based schedules and their order relative to accelerated
logical time is not claimed deterministic. They do not affect PWM in this phase.
The harness records metrics/traces, all tasks park cooperatively, Motion disables
output while parking and main prints playback after scheduler return.

## Measured results

All values below were actually generated by Debug tests and the demo; Release also
passed its corresponding scenarios and same-build replay. Metrics use every
firmware-visible control sample from acceptance through **two seconds after
completion**. Position/error/overshoot are encoder counts; times are simulated ms.

| Scenario | Start | Target | Final | Absolute error | Directional overshoot | Completion ms | Settling ms | Max abs PWM | Observation ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Positive | 1000 | 2000 | 2001 | 1 | 1 | 4350 | 4830 | 0.8 | 6350 |
| Negative | 1000 | 500 | 499 | 1 | 1 | 3100 | 3630 | 0.8 | 5100 |
| Relative -300 | 1000 | 700 | 699 | 1 | 1 | 2590 | 3220 | 0.8 | 4590 |
| Sequence 1 | 1000 | 2000 | 2001 | 1 | 1 | 4350 | 4830 | 0.8 | 6350 |
| Sequence 2 | 2001 | 500 | 499 | 1 | 1 | 5600 | 6140 | 0.8 | 7600 |
| Sequence 3 | 499 | 3500 | 3501 | 1 | 1 | 9350 | 9830 | 0.8 | 11350 |
| Sequence 4, relative -500 | 3501 | 3001 | 3000 | 1 | 1 | 3090 | 3690 | 0.8 | 5090 |
| Zero-distance | 1000 | 1000 | 1000 | 0 | 0 | 190 | 0 | 0 | 2190 |

Relative movement uses the current encoder, so the fourth sequence target is 3001,
not 3000. All valid scenario moves stayed clear of travel limits and remained in
the position tolerance throughout post-completion observation. Policy and STOP
scenarios also passed but deliberately do not claim target-convergence metrics.

Definitions:

- Acceptance time: the control sample where Motion accepts the queued command and
  enters MOVING; this is t=0 for that move's metrics. Earlier UART/queue admission
  is not the acceptance event and is excluded from convergence timing.
- Completion time: first MOVING -> IDLE control sample, relative to acceptance.
- Final position/error: last encoder sample after post-observation, not the earlier
  completion sample. Absolute error is `abs(final-target)`.
- Overshoot: maximum position beyond target in the commanded direction across the
  whole observation; negative moves use `target-position`. Zero-distance reports 0.
- Settling: first sample of the final uninterrupted position **and velocity** band
  through observation end. A later excursion resets the candidate. This is a
  finite-horizon metric, not proof that the axis remains settled forever.
- Maximum PWM: maximum absolute emitted PWM over the sampled sequence.

Settling is later than completion in nonzero moves because one-count post-disable
coast briefly produces a nonzero velocity estimate. At 1 count/10 ms, velocity is
quantized in 100 counts/s increments; the 25-count/s threshold effectively requires
unchanged counts during dwell. Twenty qualifying samples span 19 intervals, explaining
the 190 ms zero-distance completion. No instantaneous mechanical stop or active
holding torque in IDLE is claimed.

Representative actual demo lines:

```text
CONTROL t_ms=0 state=MOVING target=2000 position=1000 velocity=0.0 error=1000.0 pwm=0.8000 dwell=0
CONTROL t_ms=4350 state=IDLE target=2000 position=2000 velocity=0.0 error=0.0 pwm=0.0000 dwell=20
METRIC start=1000 target=2000 final=2001 error=1.0 overshoot=1.0 settling_ms=4830 completion_ms=4350 max_pwm=0.8000 observed_ms=6350 hash=1ae531fcbaecb861
PHASE5_OK scenario=sequence moves=4 control_hz=100 plant_hz=1000 failure_line=0
```

The complete sequence's sampled PWM/position/state/dwell hashes were
`1ae531fcbaecb861`, `936a833ae277ea72`, `d6441e4503357f8c` and `4f58538e2c6ccf8b`.
The replay CTest launches the scenario twice and compares all printed metrics and
hashes. This tests same-build repeatability across fresh processes; it is not a
cross-compiler/CPU bit-exact guarantee or a cryptographic proof.

## Tests and validation

Added **22 CTest cases**, yielding **57 total**:

| Group | Cases | Coverage |
| --- | ---: | --- |
| PID | 9 | Zero error, P, I, derivative/no setpoint kick, saturation, bounded windup, reset, exact sequence replay, invalid inputs/arithmetic |
| Motion unit | 4 | Position/velocity/dwell stability, first sample and signed count wrap, reset across STOP/new move, invalid feedback/configuration |
| Metrics unit | 1 | Both overshoot directions, re-entry-based settling, velocity-band rejection, final error/max PWM |
| Real RTOS closed-loop scenarios | 7 | Positive, negative, relative, sequence, zero-distance, range/busy/deferred-command policy, STOP coast |
| Cross-process closed-loop replay | 1 | Matching metrics, completion times and sampled output/state hashes |

All **35 Phase 1–4 tests remain present and pass without modifying their sources**.
The prior RTOS scenario assertions are unchanged. Their command semantics now execute
MOVE/STOP rather than reporting everything unimplemented; manual sensor injection
remains distinct from plant convergence. The default regression still ends in IDLE
with zero PWM, all nine valid commands delivered and zero UART TX errors. Its
historical `PHASE3_OK` marker remains for compatibility, while closed-loop output
uses `PHASE5_OK`.

Executed commands:

```powershell
./scripts/test.ps1
./scripts/test.ps1 -Configuration Release
./scripts/demo.ps1
./scripts/demo.ps1 -Scenario plant-open-loop
./scripts/demo.ps1 -Scenario plant-limits
./scripts/demo.ps1 -Scenario closed-loop
cmake -DSOURCE_ROOT=. -P cmake/CheckHalIsolation.cmake
```

| Check | Final result |
| --- | --- |
| Debug configure/build/test | **57/57 passed** |
| Release configure/build/test | **57/57 passed** |
| Default RTOS/peripheral demo | **Passed** |
| Previous open-loop plant demo | **Passed** |
| Previous travel-limit demo | **Passed** |
| Closed-loop four-move demo | **Passed** |
| HAL isolation | **Passed**, both CTest configurations and standalone invocation |
| Compiler warnings | **0 reported** |
| Compile/test errors | **0** |

Validation used the existing Windows MSYS2 UCRT64 GCC 16.1.0/CMake 4.4.0/Ninja
1.13.2 toolchain with `-Wall -Wextra -Wpedantic -Werror`. No warning suppression,
vendor changes or new dependencies were introduced. Native execution ran outside
the sandbox as required by the established FreeRTOS host limitation. Linux execution,
remote CI, wall-clock jitter and MCU timing were not validated.

## Continuation verification — 2026-09-06

The implementation, six requested documents, full validation results and file
inventory were already complete when the continuation resumed. The working tree
and documents were inspected, and the preserved Debug/Release logs confirmed their
successful final runs, including deterministic replay. The six requested build/demo
commands had already run successfully and were recorded above; no source changes
required repeating those suites.

The standalone HAL check and deterministic replay check were rerun successfully:

```powershell
cmake -DSOURCE_ROOT=. -P cmake/CheckHalIsolation.cmake
cmake "-DRUNNER=$PWD/build/host-Debug/motion_controller_closed_loop.exe" -P cmake/CheckControlReplay.cmake
```

Replay again reported identical metrics, completion times and sampled output/state
hashes across two fresh processes. Only this report changed during continuation:
the acceptance-time definition and this verification record were added. No code,
test assertions, gains or measured values changed. No Phase 6 features were added.

## Files created in Phase 5

- `firmware/control/pid.c`, `pid.h`.
- `firmware/control/motion_controller.c`, `motion_controller.h`.
- `simulator/closed_loop_main.c`, `control_metrics.c`, `control_metrics.h`.
- `tests/test_pid.c`, `test_motion.c`, `test_control_metrics.c`.
- `cmake/CheckControlReplay.cmake`.
- `docs/control.md`, `docs/phase-5.md`.

## Files modified

- `CMakeLists.txt`: version 0.5.0, control library, closed-loop executable and tests.
- `firmware/app/application.c/.h`: names/enumeration for MOVING and STOPPING; startup behavior preserved.
- `firmware/tasks/motion_task.c`: firmware-owned control loop, command handling and diagnostics.
- `firmware/rtos/runtime.c/.h`, `runtime_internal.h`: control snapshots and optional cycle handshake; disabled output on Motion parking.
- `firmware/rtos/diagnostics.c/.h`: bounded motion command results.
- `firmware/platform/host/input_host.c/.h`: explicit task-context UART frame submission.
- `firmware/platform/host/telemetry_host.c`: acceptance/rejection and control snapshots.
- `scripts/demo.ps1`: adds `closed-loop`; all prior scenarios preserved.
- `cmake/CheckHalIsolation.cmake`: also forbids simulator metrics headers in portable firmware.
- `README.md`, `docs/architecture.md`, `docs/real-time-design.md`, `docs/simulation.md`: current behavior, ownership, timing and limitations.

Existing test sources, parser/queue logic, plant implementation, peripheral driver
interfaces, kernel/port sources, dependency provenance, build/test scripts and
prior phase reports remain unchanged. Build outputs/logs are under ignored `build/`.

## Limits and Phase 6 handoff

This controller is tuned only against the deliberately simplified host plant.
There is no load variation/noise/backlash/electrical fidelity. Encoder quantization
causes coarse instantaneous velocity; the dwell does not prove mechanical rest.
IDLE applies no holding torque and STOP allows coasting. Target bounds are logical
encoder bounds, not a homing or physical-limit safety policy. The accelerated
handshake validates control order, not deterministic safety scheduling or deadlines.

Phase 6 should add full E-stop and limit actions, retained fault reasons, stall and
encoder-failure detection, qualified heartbeat/watchdog supervision and safe reset
validation. Safety must act independently of ordinary move completion and logging.
Homing, ARM compilation and Renode remain later phases. No Phase 6 work was started.

- PID implemented: **YES**
- Closed-loop position control implemented: **YES**
- Physical STM32 used: **NO**
- Physical motor/encoder used: **NO**
- Full E-stop safety implemented: **NO**
- ARM Cortex-M build: **NO**
- Renode validation: **NO**

No commit or push. Work stops after Phase 5.
