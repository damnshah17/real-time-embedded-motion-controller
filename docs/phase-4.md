# Phase 4 — deterministic motor / encoder simulation

> Historical phase report. Behavior and test counts below describe Phase 4 only.
> See [Phase 8](phase-8.md) and [protocol](protocol.md) for the current host baseline.

## Completed scope and preserved baseline

Before editing, the requested README, architecture, Phase 3 report, real-time design,
STM32 mapping, peripheral implementations, host runner, runtime, Motion task and test
configuration were inspected. The existing suite passed **19/19 Debug and 19/19
Release**. No previous tests were weakened or modified.

Phase 4 adds a host-only motor plant, explicit fixed stepping, position/velocity,
PWM-to-motion behavior, damping, speed/travel limits, raw encoder generation,
automatic limit GPIOs, deterministic reset/replay, host snapshots and independent
plant tests/demos. It preserves the FreeRTOS kernel/port, four firmware tasks and
priorities, bounded queues, notifications, heartbeats, static RTOS allocation,
driver interfaces and manual host injection behavior.

## Actual model and data path

The implementation is `simulator/plant_model.c/.h`, linked as `plant_model` into
`motion_controller_plant` and `test_plant`. It is not linked into portable firmware
or the existing RTOS runner. No new package/vendor dependency was introduced;
Unix builds link the standard math library for `round`.

| Item | Implemented value/behavior |
| --- | --- |
| Integration | Semi-implicit Euler: update velocity, then position |
| Acceleration | `motor_gain * sampled_pwm - damping * old_velocity` |
| Velocity | `clamp(old_velocity + acceleration * dt, -max_velocity, max_velocity)` |
| Position | `old_position + updated_velocity * dt`, then hard travel clamp |
| Step | **1 ms**, explicitly advanced |
| Motor gain | **2000 axis units/s²** at full duty |
| Damping | **4 s⁻¹** |
| Maximum velocity magnitude | **600 axis units/s** |
| Travel | **0..5000 axis units** |
| Default initial state | Position **1000**, velocity **0**, drive **0** |
| Encoder resolution | **1 raw count/axis unit** |
| Encoder rounding | Nearest, halfway away from zero; signed configurations supported |
| Unpowered behavior | Coast/decay, no immediate velocity reset |
| Boundary behavior | Clamp position and remove only outward velocity |

The central defaults are defined in `plant_default_config`; units and numerical
validation rules are detailed in [simulation.md](simulation.md). No electrical or
physical calibration is claimed. Default full-duty unconstrained equilibrium speed
is 500 units/s; tests configure a lower speed cap to exercise saturation directly.

The single-threaded host scenario loop owns `plant_step`. It drives the existing
`motor_set_output`/`motor_disable` API, and the model samples
`motor_get_commanded_output` once per step. Numerical computation happens outside
critical sections. One short transaction then publishes raw count and both limits
through `encoder_host_set_count` and the GPIO host setters. These drivers retain
their existing nested access-hook strategy; the standalone runner has no concurrent
tasks and uses no-op hooks. Model state is private to its owner, not shared telemetry.

Firmware continues reading `encoder_get_count` and `gpio_read_inputs`. Raw counts
change with mechanical position while the encoder driver's logical reference offset
survives every publication and plant reset. Limits activate at the exact configured
mechanical endpoints, independent of encoder rounding/reference. E-stop remains a
separately injected signal. The plant never changes controller state or raises faults.

The existing Motion task deliberately disables output every cycle. Keeping the
open-loop demo in a separate executable avoids competing PWM writers and preserves
that behavior. No extra FreeRTOS task, plant call in firmware, or moving-plant-plus-RTOS
integration was added. The latter can be composed when Phase 5 introduces control.

## Determinism and manual injection

Core stepping has no sleep, wall-clock input, random source, allocation or logging.
The runner advances manual logical time separately after each successful step.
Tests execute explicit iteration counts and compare final state fields exactly for
identical initial state/configuration/PWM sequences within one build. Cross-compiler
or CPU bitwise equivalence is not claimed.

Successful reset restores a selected valid mechanical position, zero velocity,
acceleration, sampled duty and step count, disables motor command and publishes
encoder/limits. It preserves encoder reference, E-stop and the independent platform
clock. Invalid setup preserves the previous valid model/peripherals.

Manual mode means no plant stepping: all prior injection tests remain independent.
Plant-connected mode assigns raw encoder and automatic limits to the plant; direct
manual writes last only until the next successful step/reset. Concurrent competing
writers are unsupported. Separate dynamics/publication stages leave insertion points
for later stall/frozen-encoder controls without adding those optional features now.

## Tests added

| New CTest case | Evidence |
| --- | --- |
| `plant_zero` | 1000 zero-input steps preserve position and zero velocity |
| `plant_positive` | Positive duty increases position, velocity and encoder count |
| `plant_negative` | Negative duty produces negative motion and decreasing counts |
| `plant_magnitude` | Larger duty produces stronger initial acceleration/motion |
| `plant_damping` | Disable causes monotonic speed decay and continued coasting in both directions |
| `plant_velocity` | Both signed speed caps hold over sustained drive |
| `plant_positive_limit` | Maximum travel clamps, switch activates, reversal releases it |
| `plant_negative_limit` | Minimum travel clamps, switch activates, reversal releases it |
| `plant_encoder` | Signed scaling, ties-away rounding and int32 endpoint conversion |
| `plant_reference` | Logical reference changes preserve mechanical position and subsequent count deltas |
| `plant_determinism` | Same PWM sequence replay produces identical state, visible counts and limits |
| `plant_reset` | Exact mechanical reset, drive zero, reference/E-stop preserved |
| `plant_invalid` | Missing initialization, invalid pointers/configuration, unstable step and unrepresentable ranges rejected |
| `plant_ownership` | Next step replaces manual raw/limit injections, preserves E-stop, uses bounded hook transactions |
| `plant_demo_open-loop` | Self-checking positive/coast/reverse scenario |
| `plant_demo_limits` | Self-checking scenario reaching both travel endpoints |

There are **14 plant unit cases + 2 plant scenarios = 16 additions** and **35 total
CTest cases**. All 19 Phase 1–3 cases remain present and passing. The HAL regression
guard now also rejects simulator/plant headers and plant calls in portable sources,
including a future `firmware/control` directory. It remains a text-based regression
guard, not a full preprocessor dependency proof.

## Executed validation

Validation used the existing Windows MSYS2 UCRT64 x64 GCC 16.1.0, CMake 4.4.0 and
Ninja 1.13.2 toolchain. Native build/test/demo processes ran outside the sandbox
because of the previously established host execution limitation.

```powershell
./scripts/test.ps1
./scripts/test.ps1 -Configuration Release
./scripts/demo.ps1
./scripts/demo.ps1 -Scenario plant-open-loop
./scripts/demo.ps1 -Scenario plant-limits
cmake -DSOURCE_ROOT=. -P cmake/CheckHalIsolation.cmake
```

| Check | Final observed result |
| --- | --- |
| Debug configure/build/test | **35/35 passed** |
| Release configure/build/test | **35/35 passed** |
| Previous RTOS peripheral demo | **Passed**, `PHASE3_OK`, IDLE, nine commands delivered |
| Plant open-loop demo | **Passed**, `PHASE4_OK`, 4000 explicit steps |
| Plant limit demo | **Passed**, `PHASE4_OK`, 2001 total steps across explicit reset |
| HAL isolation | **Passed** in both CTest configurations and standalone invocation |
| Compiler warnings | **0 reported** |
| Compiler/test errors | **0** |

All new C sources compiled in Debug and Release with the existing
`-Wall -Wextra -Wpedantic -Werror` policy. No warning suppression was introduced.
The final Debug rerun also verified the small logging adjustment that avoids
duplicating a periodic snapshot at a segment endpoint. No code changes followed
the final validation. Linux execution and remote GitHub CI were not run.

Actual open-loop observations (units and units/s):

| Simulated time | Stage / sampled PWM | Position | Velocity | Raw / visible encoder |
| ---: | --- | ---: | ---: | --- |
| 0 ms | Initial / 0 | 1000.000000 | 0.000000 | 1000 / 1000 |
| 1000 ms | Positive / +0.5 | 1188.881040 | 245.457673 | 1189 / 1189 |
| 2000 ms | Coast / 0 | 1248.889511 | 4.459796 | 1249 / 1249 |
| 4000 ms | Reverse / -0.5 | 812.229083 | -249.915997 | 812 / 812 |

The runner disabled motor command before exiting, retaining its final velocity
state. No instantaneous mechanical stop is implied. No target settling time,
overshoot or tracking-error metric is reported.

Actual limit observations:

```text
stage=positive_limit logical_ms=1000 position=5000.000000 velocity=0.000000 raw=5000 encoder=5000 limits=0/1
stage=leave_positive_limit logical_ms=1001 position=4999.999000 velocity=-1.000000 raw=5000 encoder=5000 limits=0/0
stage=negative_limit logical_ms=2001 position=0.000000 velocity=0.000000 raw=0 encoder=0 limits=1/0
```

Limit ordering is negative/positive. The released positive limit while the rounded
encoder remains 5000 illustrates why switches derive from mechanical position, not
logical encoder count. The explicit intermediate reset starts near the other end;
it is scenario setup, not HOME. No FAULT or ESTOP behavior was introduced.

## Files created and modified

Created:

- `simulator/plant_model.c`
- `simulator/plant_model.h`
- `simulator/plant_main.c`
- `tests/test_plant.c`
- `docs/simulation.md`
- `docs/phase-4.md`

Modified:

- `CMakeLists.txt` — version 0.4.0, host plant library/executable and 16 new cases.
- `cmake/CheckHalIsolation.cmake` — guard against simulator/plant dependencies.
- `scripts/demo.ps1` — adds `plant-open-loop` and `plant-limits`; default RTOS scenario preserved.
- `README.md` — current scope, usage, parameters and validation links.
- `docs/architecture.md` — plant data-flow diagram, ownership and future STM32 replacement.
- `docs/real-time-design.md` — simulation time, concurrency and numerical limits.

No firmware source/header, existing test source, vendored FreeRTOS file, build/test
script or prior phase report was changed. Generated build products/logs are under
ignored `build/`.

## Limitations and Phase 5 handoff

This is a simplified host model, not a scientific motor simulator. Electrical
dynamics, gearbox, compliance, backlash, load variation, noise, quadrature pulse
timing and electrical GPIO behavior are absent. Hard stops have no rebound or
switch hysteresis. A fixed numerical step does not prove real-time host deadlines
or MCU timing. One connected axis and one model owner are supported.

Phase 5 should add reusable PID, targets, MOVE execution, PWM saturation/control
policy and move-completion logic, then connect a clearly ordered control/plant
schedule and collect actual closed-loop results. Full safety, homing, stall and
encoder-timeout detection, watchdog expiration, ARM and Renode remain later work.

- PID implemented: **NO**
- Closed-loop movement implemented: **NO**
- Full safety state actions implemented: **NO**
- Physical STM32 used: **NO**
- Physical motor/encoder used: **NO**
- ARM Cortex-M build: **NO**
- Renode validation: **NO**

No commit or push. Work stops after Phase 4.
