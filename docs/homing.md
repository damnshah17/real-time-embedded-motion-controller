# One-stage homing and reference establishment

HOME seeks the negative limit using fixed PWM **-0.30** through the existing motor
gate. It does not use the position PID. The Motion task owns the portable homing
module and executes one bounded update per 10 ms control release. Safety retains
priority 4, its 20 ms supervision period and final inhibit authority.

## State and command policy

```mermaid
stateDiagram-v2
    IDLE --> HOMING: HOME accepted
    HOMING --> HOMING: negative switch inactive; PWM -0.30
    HOMING --> IDLE: negative switch active; zero output; set reference
    HOMING --> STOPPING: STOP; aborted; zero output
    STOPPING --> IDLE: existing low-speed dwell
    HOMING --> ESTOP: GPIO or software ESTOP
    HOMING --> FAULT: safety fault or 40000 ms timeout
```

HOME is accepted only from safe IDLE with valid feedback. HOME from MOVING,
STOPPING, HOMING, FAULT or ESTOP is rejected. MOVE, MOVE_REL, SPEED and RESET are
rejected during HOMING. STATUS and HELP remain available. Command queue admission
is still distinct from execution acceptance and successful completion.

STOP cancels the home without changing the encoder reference. It clears PID/output
and enters the existing STOPPING state until 20 low-velocity samples establish the
configured dwell. The plant can coast. The reported home outcome is STOPPED, never
COMPLETE. A fresh HOME is required after returning to IDLE.

GPIO and software ESTOP interrupt homing through the existing safety latch. Other
safety faults also abort. Outcomes distinguish COMPLETE, STOPPED, ESTOPPED, FAULTED
and TIMEOUT. Successful homes have their own counter; they do not increment the
ordinary completed-move counter. Aborts never establish a new reference, and RESET
never resumes the abandoned seek. The last outcome remains diagnostic history.

## Seek output and timeout

The default plant has motor gain 2000 and damping 4. At duty magnitude 0.30, its
steady-state speed is approximately `2000 * 0.30 / 4 = 150` counts/s. A full
5000-count approach therefore takes about 33.3 seconds plus acceleration. The fixed
**40000 ms** timeout leaves margin without reducing the budget to accelerate tests.
Both constants are centralized in `firmware/control/homing.h`.

Duration uses unsigned millisecond subtraction, including timer wrap. At elapsed
time >=40000 ms the seek ends with HOMING_TIMEOUT, motor output zero and retained
FAULT. A switch first observed exactly at the deadline does not override timeout.
RESET still checks GPIO, heartbeat health and any other active recovery requirement.
It acknowledges this expired attempt and permits a fresh attempt; it cannot prove
an unseen home sensor has been repaired while stationary. The timeout fixture is
restored before retry, and the second HOME is required to succeed independently.

## Sensor and reference

Motion reads the existing GPIO abstraction, never plant position. A negative switch
event during an active, otherwise safe HOME is expected. The Safety task can inhibit
output on that event before Motion's next release. Motion also checks inputs on
every home update and reconciles safety before changing the reference. Positive
limit, E-stop and other faults retain priority; there is no global limit bypass.

At a valid negative switch event, within the existing synchronized control update:

1. Command zero motor output.
2. Call `encoder_set_reference(0)`.
3. Set controller position/target/error and velocity to zero.
4. Reset PID integral and derivative history, and completion dwell.
5. Reset encoder velocity sampling history, so the next sample initializes it.
6. Report COMPLETE, increment the home counter and return to IDLE at zero output.

The host encoder implements `logical = raw + reference_offset` with defined
modulo-32-bit arithmetic. Re-referencing updates only the offset. It does not edit
the plant or raw counter. A test uses physical minimum 100, raw count 100 and a
starting logical position of 7777. HOME leaves mechanics/raw at 100, sets the offset
to -100 and reports logical zero. The next unchanged sample reports velocity zero;
subsequent real count changes again produce ordinary encoder-derived velocity.

The host-only read-only encoder snapshot reports the actual published raw count and
offset. This matters during frozen feedback: physical position can keep changing
while the published raw count and reference offset remain unchanged. Metrics do not
mistake that difference for a new reference offset.

## Already active switch and the selected departure allowance

An active negative switch while HOME is accepted from IDLE requires no negative
drive: output stays zero, reference is established and HOME completes in the
acceptance update. Tests cover this condition and repeated HOME while parked.
An **already latched** negative-limit FAULT is not cleared by HOME. In particular,
an active unrecognized limit observed at startup retains Phase 6 fault semantics:
service must clear the condition and RESET before HOME can be accepted. The
already-active scenario presents contact while IDLE, then submits HOME before the
next periodic Safety observation; it does not bypass a fault latch.

One-stage HOME deliberately leaves the switch active. The user-selected policy is
a narrow post-home allowance, granted only by successful HOME:

| Situation | Output / command behavior |
| --- | --- |
| Parked after HOME, switch active | Full gate inhibited; output stays zero |
| Negative or zero-distance departure | MOVE/MOVE_REL rejected |
| Explicit positive MOVE/MOVE_REL | Existing travel validation, then positive departure permitted |
| During departure, switch still active | Atomic directional gate rejects negative duty |
| Switch first observed released | Allowance and directional restriction removed |
| Later unexpected negative switch activation | Normal retained NEGATIVE_LIMIT fault |
| Any E-stop or fault | Full inhibit; departure allowance revoked |

The positive target must be strictly greater than the current logical position.
While contact remains active, Safety permits only parked zero output or an active
positive departure. Negative PID/stale requests cannot bypass the directional gate.
Once release is observed, all ordinary limit rules resume. There is no backoff,
second approach, recovery jog or automatic movement after HOME. RESET while the
negative switch remains active still rejects under the existing physical-input
validation; it is not needed for the authorized positive departure.

Both the full inhibit and directional guard use the same short driver access
critical sections as duty writes. Homing acceptance, reference changes, safety state,
completion and departure authorization share the RTOS control synchronization.
No new task, queue, mutex or heap allocation was introduced. Normal PID gains and
ordinary control scheduling are unchanged.

## Fault supervision

NO_MOTION_UNDER_COMMAND applies to HOMING as well as MOVING. PWM -0.30 meets the
existing inclusive 0.30 threshold. Encoder-derived speed <=25 counts/s continuously
for 500 ms faults before the 40-second timeout. Expected negative contact is excluded
from that candidate because it completes HOME. Mechanical stall and frozen feedback
retain the same honest classification; firmware cannot distinguish them from these
signals alone. Existing encoder-displacement evidence is required for no-motion RESET.

Homing tests inject both conditions and verify no false home success, zero output,
retained fault, invalid RESET while unresolved, service/feedback restoration,
explicit RESET and successful HOME afterward. Positive-limit and dropped-heartbeat
tests verify other safety interruptions. Both GPIO and queued software ESTOP are
tested, including recovery followed by a new HOME.

## Deterministic runner, metrics and demos

The existing runner coordinates ten 1 ms plant steps, a 20 ms Safety release when
due and a 10 ms Motion release. It never calls the firmware homing implementation
directly. HOME enters through UART, Communications and the existing command queue.
The original tick-based runner still exercises periodic scheduling and ISR input.

```powershell
./scripts/demo.ps1 -Scenario homing
./scripts/demo.ps1 -Scenario homing-estop
./scripts/demo.ps1 -Scenario homing-timeout
```

Playback is printed after cooperative task shutdown. Timestamped state/contact
transitions show seek, zeroing, positive departure and switch release; operation
metrics are grouped separately. No line is printed for every 10 ms update.

| Start logical / raw | Physical minimum | Seek duration | Final logical / raw | Final PWM |
| --- | ---: | ---: | --- | ---: |
| 500 / 500 | 0 | 3590 ms | 0 / 0 | 0 |
| 1500 / 1500 | 0 | 10250 ms | 0 / 0 | 0 |
| 4000 / 4000 | 0 | 26920 ms | 0 / 0 | 0 |
| 7777 / 1600 | 100 | 10250 ms | 0 / 100 | 0 |

The timeout scenario places the host negative hard stop/sensor at -10000 instead
of 0. The motor continues moving, so no-motion protection does not mask the timeout;
it cannot reach the switch within the unchanged 40-second budget. This is an
explicit test environment, not a claimed normal travel configuration or a firmware
fault-injection command. Normal mechanics are restored before RESET and retry.

## Limits of this phase

One switch approach has no debounce, hysteresis, backoff, slow re-approach or encoder
index capture. The ideal simulated hard stop removes outward velocity; zero PWM
alone would not establish physical rest on real equipment. Switch repeatability,
noise, mechanical tolerances, offset calibration and electrical latency are not
measured. MOVE does not require prior homing, preserving the Phase 5/6 command policy;
an application-level reference-valid interlock would be a separate requirement.

These are host-simulation results, not certified machine safety, STO, safety relay
behavior or IEC 61508 / ISO 13849 compliance. No physical STM32/motor/encoder, ARM
binary or Renode validation was used. Target implementation and advanced homing
remain outside Phase 7. See [validation report](phase-7.md).
