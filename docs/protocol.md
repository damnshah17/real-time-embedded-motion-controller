# Host-validated firmware protocol

Commands are uppercase ASCII frames, at most 63 bytes, with optional surrounding
space/tab/CR/LF. Numeric arguments are signed 32-bit decimal integers, optionally
prefixed by + or -. Fractions, overflow, extra arguments and embedded NULs in
arguments are rejected. UART transports complete frames; byte timing and physical
serial ports are not modeled.

| Syntax | Actual behavior |
| --- | --- |
| `MOVE <position_counts>` | From safe IDLE, validate logical target 0..5000 and begin PID move |
| `MOVE_REL <offset_counts>` | Add offset to sampled encoder position using wide arithmetic, then the same validation/move |
| `HOME` | From safe IDLE, fixed -0.30 negative seek; negative contact establishes logical zero; 40000 ms timeout |
| `STOP` | Remove drive when consumed; active move/home enters STOPPING until low-speed dwell; no reference on aborted HOME |
| `ESTOP` | Latch ESTOP and zero output when consumed; physical GPIO E-stop is independently supervised |
| `RESET` | Reevaluate safety conditions; clear only recoverable latch; zero output and no automatic resume |
| `STATUS` | Request one coherent snapshot, captured when Telemetry services the diagnostic |
| `HELP` | List syntax and brief purpose for all eight supported commands; label SPEED deferred |
| `SPEED <integer>` | Parsed but intentionally unsupported; `ERR NOT_IMPLEMENTED` in ordinary healthy operation |

MOVE/HOME reject while busy or safety locked. SPEED also rejects with INVALID_STATE
in HOMING/FAULT/ESTOP. STATUS/HELP remain available in unsafe states. STOP does not
clear an unsafe latch. See [control](control.md), [safety](safety.md) and
[homing](homing.md) for complete admission/recovery rules. Post-home contact permits
only a positive target departure; the allowance ends on switch release.

## Queue admission, acceptance and completion

The existing response convention is retained:

```text
ACK QUEUED command=MOVE value=2000
MOTION command=MOVE value=2000 ACK ACCEPTED
MOTION COMPLETE output=zero
```

QUEUE admission is not execution acceptance. Motion can subsequently return a
state/range error. ACK ACCEPTED means the operation started or the synchronous
action was applied; MOVE/MOVE_REL completion occurs later after stable arrival.
HOME similarly emits acceptance, then `HOMING COMPLETE reference=0 applied_pwm=0`
on success. Already-active home contact can complete in the acceptance update.
State, `moves_completed` and home result/completion counters provide independent
snapshot evidence. STOP/ESTOP/FAULT aborts do not increment successful completion.

There are no request IDs, guaranteed delivery, retry deduplication or persistent
event history. The bounded diagnostic queue may drop an ACK/error/completion under
overload. STATUS itself can be lost under that same policy. Command processing and
safety never wait for output. Software ESTOP shares the ordinary queue; it is not
an out-of-band emergency channel.

## Errors

| Response | Meaning |
| --- | --- |
| `ERR EMPTY` | Empty/whitespace frame |
| `ERR UNKNOWN_COMMAND` | Unknown or incorrectly cased verb, including SIM |
| `ERR INVALID_ARGUMENT` | Missing/extra/malformed/overflowing argument |
| `ERR LINE_TOO_LONG` | Frame length at least 64 bytes |
| `ERR UART_RX` | Transport receive failure |
| `ERR QUEUE_FULL command=...` | Syntactically valid command could not enter queue |
| `MOTION command=... value=... ERR OUT_OF_RANGE` | Integer parsed, but resulting target outside configured travel |
| `... ERR INVALID_STATE` | Busy, startup, unsafe state or invalid RESET conditions |
| `... ERR NOT_IMPLEMENTED` | Recognized deferred command, currently SPEED |
| `... ERR INPUT` | Controller feedback/input failure (normally retained INTERNAL through RTOS integration) |

The existing enums/names are preserved. No new SAFETY_LOCKED synonym was added.
Diagnostic-integrity errors remain INTERNAL/DIAGNOSTIC_KIND; they are not command
syntax errors. Invalid parse leaves the output command unchanged.

## STATUS and telemetry schema

Phase 8 replaces the old “see periodic snapshot” STATUS response and the mixed
HOME/SAFETY/TEL/CONTROL periodic lines. It is an intentional diagnostic-format
change; command admission and motion acceptance formats are unchanged.

```text
[000100] ACK STATUS snapshot_ms=110
STATUS t_ms=110 motion state=IDLE target_counts=500 position_counts=500 velocity_counts_s=0 error_counts=0 feedback_valid=1
STATUS t_ms=110 safety requested_pwm=0 applied_pwm=0 fault=NONE fault_ms=0 estop=0 limit_neg=0 limit_pos=0 inputs_valid=1
STATUS t_ms=110 health motion_fresh=1 safety_fresh=1 comms_fresh=1 watchdog_supervision=HEALTHY refreshes=5
STATUS t_ms=110 home result=NONE referenced=0 elapsed_ms=0 completed=0 contact_allowance=0
STATUS t_ms=110 counters queue_used=0 queue_capacity=8 queue_high_water=1 diagnostic_drops=0 moves_completed=0 aborted=0
```

This is an illustrative format, not a recorded scenario. All five lines share one
immutable runtime snapshot and timestamp, copied under the existing critical section.
The bracketed time is command-consumption time; `snapshot_ms` is later service time.
Sensor/control values are the latest published firmware observations, not simultaneous
new hardware reads. Safety may already have latched a fault while Motion's last
published target/home result still describes the interrupted operation; state/fault
and applied PWM expose that safety override until Motion synchronizes.

Periodic telemetry uses the identical five groups with prefix `TEL`, at the existing
10 Hz low-priority schedule in every state. There is no per-control-cycle printing.
Each UART write is bounded to 256 bytes. Formatting and TX occur outside critical
sections, and no raw plant position/velocity or simulator flag enters this schema.

`requested_pwm` is the driver's latest request; `applied_pwm` is current gated duty.
`watchdog_supervision` summarizes current critical heartbeat freshness, not hardware
expiry status or permission to move. It can be HEALTHY in a latched ESTOP.
`referenced` records successful completion of the latest HOME attempt; beginning
another HOME clears that flag but does not erase the old encoder offset.

## Units and host controls

| Quantity | Unit / configured default |
| --- | --- |
| Position, target, error, tolerance | Logical encoder counts; tolerance 3 counts |
| Velocity / completion threshold | Counts/s; threshold 25 counts/s |
| PWM and homing PWM | Normalized signed duty [-1,+1]; PID ±0.8, HOME -0.30 |
| PID Kp / Ki / Kd | PWM/count, PWM/(count·s), PWM/(count/s) |
| Motion / Safety / Telemetry | 10/20/100 ms = 100/50/10 Hz |
| Plant | 1 ms numerical step = 1000 steps/s of simulated time |
| Completion dwell | 20 qualifying samples; 19 intervals from first to last |
| Heartbeat freshness / watchdog timeout | 150 ms / 1000 ms |
| No-motion qualification / HOME timeout | 500 ms / 40000 ms |
| All timestamps/timeouts | Unsigned milliseconds, modulo 2^32 |

Default plant scaling is one raw count per axis unit. No millimeter/revolution
calibration is implied. Metrics distinguish compiled configuration from measured
logical-time results; neither establishes hardware timing.

There is **no SIM text-command parser**. `SIM ESTOP ON`, `SIM STALL ON` and similar
examples are not accepted firmware commands or an implemented host console syntax.
Named host scenarios use `gpio_host_set_estop`, plant `stalled`/`encoder_frozen`
flags and the runtime heartbeat observation hook. Those controls remain in host
setup/scenario code. Portable firmware reads driver signals, never injection flags.
