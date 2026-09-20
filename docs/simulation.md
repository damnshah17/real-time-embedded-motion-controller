# Deterministic motor and encoder simulation

The host motor model is a deliberately simplified deterministic plant used to
exercise the implemented firmware architecture, control and safety logic. It does not model motor
electrical dynamics, gearbox effects, compliance, backlash, load variation, sensor
noise, or other physical-machine behavior with hardware fidelity.

The preserved Phase 4 runner provides open-loop **PWM -> plant -> encoder/limits**.
Phase 5 adds a separate runner coordinating this same plant with the real FreeRTOS
Motion task's PID. The plant itself still knows nothing about targets, PID, homing,
safety transitions or watchdog policy. See [control design](control.md).

## Parameters, units and equations

`plant_default_config()` in `simulator/plant_model.c` is the central configuration:

| Parameter | Value | Unit |
| --- | ---: | --- |
| `step_ms` | 1 | simulated milliseconds per step |
| `motor_gain` | 2000 | axis units/s² at full duty |
| `damping` | 4 | s⁻¹ |
| `max_velocity` | 600 | axis units/s |
| `min_position` | 0 | axis units |
| `max_position` | 5000 | axis units |
| `encoder_counts_per_unit` | 1 | raw counts/axis unit |
| Default initial position | 1000 | axis units |

Axis units are logical coordinates. At default resolution one axis unit maps to
one raw encoder count; no millimeter/revolution calibration is implied. The parser's
integer MOVE values now use this count coordinate in the firmware controller.
Gain and damping yield an unconstrained full-duty equilibrium speed of
2000/4 = 500 units/s; the independent 600 units/s cap bounds configured models.
These are illustrative simulation parameters, not identified motor measurements or
PID tuning results.

Each call samples the existing motor command once and performs:

```text
dt = step_ms / 1000
a = motor_gain * sampled_pwm - damping * old_velocity
v = clamp(old_velocity + a * dt, -max_velocity, +max_velocity)
x = old_position + v * dt
if x <= min_position: x = min_position; remove negative velocity
if x >= max_position: x = max_position; remove positive velocity
raw_count = round(x * encoder_counts_per_unit)
negative_limit = (x <= min_position)
positive_limit = (x >= max_position)
```

This is semi-implicit Euler: velocity is updated before position. `acceleration`
in a snapshot is the drive/damping acceleration before speed/contact constraints;
it does not include a collision impulse. Hard stops are perfectly inelastic in the
outward direction. Reversing duty can move inward and release a switch on the next
step. No bounce, compliance, switch hysteresis or firmware fault is simulated.

Zero duty and `motor_disable()` remove drive without deleting velocity. Damping
then produces coast/decay. With defaults, each unpowered step multiplies velocity
by 0.996. There is no artificial velocity snap-to-zero, except outward velocity at
a hard boundary or an injected mechanical stall. There is no distinct modeled
enable pin or brake; the motor driver's safety latch forces zero duty.

Configuration must be finite, have positive gain/speed/resolution, nonnegative
damping, ordered travel endpoints and an integral step between 1 and 1000 ms.
The damping-step product must be <=1, preventing oscillatory unpowered decay.
Intermediate arithmetic bounds and the entire raw encoder travel range must be
representable. Initial/reset positions outside travel are rejected, not silently
clamped. These guards bound this simple model; they are not a general numerical
accuracy guarantee. Large accepted steps still reduce integration accuracy.

## Encoder and reference

Conversion uses C `round`: nearest integer, halfway values away from zero. Signed
travel configurations are supported, including negative counts. The configured
raw count range must fit `int32_t`; impossible ranges are rejected at initialization.
No undefined floating-to-integer conversion or silent raw-count saturation is used.

`encoder_host_set_count` changes only raw state. Firmware reads through the existing
`encoder_get_count`, which adds its reference offset with defined modulo-2^32
semantics. For example, resetting the logical reference to zero at physical count
1000 makes raw count 1025 read as 25. The plant does not move mechanically when
`encoder_set_reference` is called and never resets that offset itself.

## Ownership, reset and injection

`motion_controller_plant` owns a single zero-initialized `plant_model_t`; it calls
`platform_init`, `plant_init`, then explicit `plant_step` calls. The public model
mechanical/configuration members are read-only to callers; use initialization/reset functions to change
mechanics/configuration. `plant_snapshot` copies state for the same owner, not for
unsynchronized cross-thread readers. Only one model may publish to the singleton
host axis peripherals at a time.

The standalone open-loop runner does not start FreeRTOS, so it cannot compete with
Motion's PWM ownership. The periodic RTOS runner still validates startup/tasks/queues/
notifications and manual peripheral injection. The
plant demo writes only through `motor_set_output`/`motor_disable`, samples through
`motor_get_commanded_output`, and exposes results through the same encoder/GPIO
implementations that firmware uses. Firmware never calls plant code.

PWM sampling is a short protected driver read. All arithmetic is outside access
hooks. A second, short transaction publishes raw encoder and both limit states
together, using the existing nestable host access hooks. E-stop is not modified.
In the single-threaded open-loop executable hooks are no-ops. The Phase 5 closed-loop
runner binds the existing task critical-section hooks and retains one model owner
with an explicit request/completion handshake. This API is not safe for arbitrary native producer
threads or ISR use. No mutex, heap allocation or new task was added.

Successful init/reset disables motor drive, sets the selected position, zeroes
velocity/acceleration/applied PWM/step count, and publishes derived peripherals.
Encoder logical reference and E-stop survive. Platform time is not reset. Invalid
setup leaves the existing model and peripherals unchanged. `plant_step` rejects an
uninitialized model and refuses to wrap its 64-bit step counter. Peripherals must
be initialized before connection and must not be reinitialized concurrently.

There are two usage modes, determined by the runner:

- **Manual:** do not step a plant. Existing encoder/GPIO injection holds until
  explicitly changed. Driver contracts remain stable; Phase 6 Safety now responds
  to those inputs, so active E-stop in the peripheral demo remains latched.
- **Plant-connected:** the plant is the sole intended writer of raw counts and
  automatic limits. A direct manual write is overwritten at the next successful
  step/reset. Do not race two producers. E-stop injection and encoder referencing
  remain independent controls.

The scenario owner's `stalled` flag holds physical position and sets velocity zero;
`encoder_frozen` skips only encoder publication while physical motion and limits
continue. Both are host-only controls, never firmware UART commands. Init/reset
clears these flags. Firmware classifies both from PWM and encoder response as
NO_MOTION_UNDER_COMMAND. See [safety](safety.md) for recovery evidence requirements.

## Determinism, demos and observability

Tests advance explicit step counts with no sleeps or wall-clock reads. Same-build
replay compares each final state field exactly, plus firmware-visible encoder and
limit states. It avoids comparing struct padding. Cross-platform/compiler bitwise
identity is not guaranteed; the model uses ordinary C double arithmetic, not fixed
point or hardware-calibrated physics.

The runner separately advances the manual platform timer by the configured step
after each update. The existing 10 ms RTOS timer is a different execution mode;
1 ms numerical stepping does not change task frequency or demonstrate a 1 kHz
real-time control loop. `plant_step` itself neither advances the timer nor logs.

```powershell
./scripts/demo.ps1 -Scenario plant-open-loop
./scripts/demo.ps1 -Scenario plant-limits
./build/host-Debug/motion_controller_plant.exe --quiet --scenario open-loop
```

Open-loop: start at 1000 with zero velocity; +0.5 duty for 1000 steps, disable for
1000, then -0.5 for 2000. Assertions check forward motion/counts, continued coasting
with reduced velocity and reversal. Limits: start 10 units below maximum, drive +1
for 1000 steps, confirm hard stop/switch, reverse for one step to release; reset 10
units above minimum and drive -1 for 1000 steps to confirm the other hard stop.
The reset is explicit test setup, not HOME or an instantaneous physical move claim.

Host snapshots report PWM sampled for the last step, position, velocity, raw count,
firmware-visible count and negative/positive limits every 500 logical milliseconds
plus segment endpoints. Output comes from simulator infrastructure, not portable
firmware telemetry. `--quiet` retains only a self-check result. On exit the runner
disables motor command but retains the final mechanical velocity; it does not claim
the axis physically stopped when no further steps are executed.

See [Phase 4 validation](phase-4.md) for historical open-loop results. Closed-loop
metrics are separately defined and recorded in [control](control.md) and the
[Phase 5 report](phase-5.md).

## Phase 5 connection to the real Motion task

`motion_controller_closed_loop` initializes this unchanged plant at 1000 units,
then starts the existing four FreeRTOS tasks plus its host harness. The harness
advances ten plant steps under the previous output, advances logical time by 10 ms,
releases Motion once and waits for its completion notification. Motion reads
encoder feedback and runs PID; the harness never calls PID. Ordinary motion uses
only Motion's duty requests; targeted safety tests also submit stale requests to
verify inhibition and outward drive shutdown at limits.
The 1000:100 Hz plant/control ratio is fixed in logical time and independent of
Windows elapsed time. The plant equation/configuration remains unchanged.

Scenario commands are submitted as bounded UART frames, parsed by the real Comms
task and passed through its existing command queue. This is a task-context injection
path, separate from the retained tick/FromISR input regression. Safety now executes
every 20 logical milliseconds via its own notification handshake, before Motion at
coincident releases. Comms is notified each 10 ms cycle; its heartbeat filter can
suppress selected observations without stopping tasks. Telemetry remains scheduled
by kernel ticks and is noncritical. All firmware control diagnostics contain only encoder-derived
values. Metrics and sparse trace playback come from host infrastructure after shutdown.

## Phase 7 homing scenarios

The same runner now sends HOME through UART and the real tasks. Default homing
starts are 500, 1500 and 4000 counts. The reference test changes the host physical
minimum to 100 and maximum to 5100, starts mechanics/raw at 1600 and sets the
initial logical reference to 7777. Successful firmware homing leaves mechanics/raw
at 100 and changes only the offset to -100. A read-only host encoder snapshot makes
published raw counts and actual reference offset visible even during encoder freeze.

The 40-second timeout fixture moves the negative hard stop/sensor to -10000, beyond
the distance reachable within the unchanged seek budget. The plant keeps moving,
so no-motion supervision does not mask timeout. No firmware code knows this fixture.
After timeout the host restores normal mechanics before explicit RESET and retry.

Homing interruptions use the existing E-stop, stall, frozen-encoder and heartbeat
controls. Tests cover positive limit, STOP, repeat HOME, MOVE after HOME and revoking
the positive-departure allowance on switch release. `homing`, `homing-estop` and
`homing-timeout` are available through `demo.ps1`. See [homing](homing.md) for metrics
and the already-active sensor policy. No homing function writes physical plant state.

The open-loop and closed-loop executables are alternative owners of the singleton
host peripherals. Manual injection remains useful for targeted unit/regression tests;
do not mix manual raw-count/limit writers with a connected moving plant.

## Phase 8 metrics and demo catalog

`scripts/metrics.ps1` runs existing positive/negative control, E-stop, stall,
watchdog and three HOME starting-position scenarios. It parses their measured
records and exports human-readable output plus JSON. The runner's host-only
`--configuration-json` mode reads compiled defaults and initialized watchdog
configuration without starting the scheduler. CTest and demo metadata supply counts.
Warning counts are unmeasured by incremental metrics builds, explicitly recorded
as null; final clean build logs supply separate compiler evidence.

`scripts/demos.ps1` derives its catalog from `demo.ps1`, runs all 18 public scenarios
and saves their output. README recommends closed-loop, homing and estop as a short
presentation sequence in independent processes. Existing PHASE1/3/4/7 success
markers are retained for their runners; they identify a compatibility format,
not the current project version. No interactive SIM parser was added: host injection
is scenario code and [firmware commands](protocol.md) reject SIM syntax.
