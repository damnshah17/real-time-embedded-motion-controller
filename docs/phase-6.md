# Phase 6 - Safety / fault handling report

> Historical phase report. Behavior and test counts below describe Phase 6 only.
> See [Phase 8](phase-8.md) and [protocol](protocol.md) for the current host baseline.

## Scope and configuration

Phase 6 adds portable fault and safety policy modules, a synchronized RTOS adapter,
a final motor-output inhibit gate and host fault scenarios. Existing FreeRTOS
V11.2.0, four firmware tasks, queues, notifications, driver boundary, PID and plant
are retained. No commit or push was made. Work stops before Phase 7 homing.

| Item | Implemented behavior / configuration |
| --- | --- |
| Safety task | Priority 4, 50 Hz / 20 ms; above Motion priority 3 |
| Motor authority | Driver gate atomically forces zero; stale requests return MOTOR_INHIBITED |
| FAULT | First reason/time/source retained, active move aborted, unsafe commands rejected |
| ESTOP | Distinct latch; physical GPIO and software command both force safe output |
| Physical input | Sampled by Safety; clearing alone does not clear ESTOP |
| Software ESTOP | Takes effect when consumed from existing bounded command queue |
| Limits | Either active switch faults in any operational state; no recovery jogging |
| No-response rule | MOVING, valid feedback, absolute PWM >=0.3 and absolute encoder velocity <=25 counts/s continuously for 500 ms |
| Encoder freeze | Same NO_MOTION_UNDER_COMMAND classification and 500 ms timeout; no invented ability to distinguish stall |
| Critical health | Motion, Safety, Communications; fresh through age 150 ms |
| Startup grace | 200 ms before latching WATCHDOG; unhealthy output inhibited during grace |
| Watchdog | Option A: Safety trips WATCHDOG and stops refresh on unhealthy records; peripheral timeout 1000 ms |
| Telemetry | Noncritical; failure cannot prevent gate action |
| RESET | Fresh physical/health validation, required encoder recovery evidence, PID/target cleared, IDLE with zero drive |
| Unsafe commands | MOVE, MOVE_REL, SPEED, HOME rejected in FAULT/ESTOP; STATUS/HELP allowed; STOP retains latch |
| Abort | Separate aborted counter; no completed-move increment; PID/dwell cleared |
| Synchronization | Short task critical sections for gate/state/RESET and scalar control update plus publication; no new mutex |
| INTERNAL | Retained control/GPIO integrity failure; service and restart required |

Kp=0.006, Ki=0.0002 and Kd=0.0015 are unchanged. Control remains 100 Hz and plant
1000 Hz, PWM +/-0.8, travel 0..5000 counts. Completion remains error <=3 counts,
estimated speed <=25 counts/s for 20 consecutive updates.

## Recovery evidence and limitations

After no-motion detection, removing a stall at rest does not prove feedback health.
RESET requires at least two counts of observed displacement while drive is inhibited.
The stall scenario models manual service displacement after removing the obstruction;
unfreezing the encoder reveals the moving plant's changed position. This limited
firmware-visible evidence is documented rather than reading simulator flags from
firmware. It is not proof of safe mechanics or comprehensive encoder integrity.

Any active limit inhibits both directions. Service must clear the switch before
RESET; there is no automatic movement away. Limit tests position the plant at each
endpoint and explicitly restore an outward PWM request after setup, then verify
Safety removes it. The standalone plant tests still cover natural endpoint contact.

The motor gate is checked immediately after each Safety handshake, before the next
Motion release. Unit and scenario tests attempt stale positive and negative PWM
under the latch. Releasing the gate does not replay these requests. Short scalar
control/update publication is serialized with safety so a detected fault cannot
race a successful completion. No critical-section MCU timing measurement is claimed.

Only Safety calls watchdog_refresh. Missing observations are injected while tasks
continue running, including the Safety task. The host watchdog does not autonomously
expire or reset a stuck process. A future hardware IWDG and independent hazardous
energy removal must cover a stopped supervisor/kernel/CPU.

## Measured deterministic response

| Scenario | Injection time | Detection / zero time | Injection to detection | Detection to zero |
| --- | ---: | ---: | ---: | ---: |
| Moving GPIO E-stop | 530 ms | 540 ms | 10 ms | 0 ms |
| Idle GPIO E-stop | 20 ms | 40 ms | 20 ms | 0 ms |
| Mechanical stall | 530 ms | 1060 ms | 530 ms | 0 ms |
| Encoder freeze | 530 ms | 1060 ms | 530 ms | 0 ms |
| Dropped Motion heartbeat | 330 ms | 500 ms | 170 ms | 0 ms |
| Dropped Safety heartbeat | 330 ms | 480 ms | 150 ms | 0 ms |
| Dropped Communications heartbeat | 330 ms | 500 ms | 170 ms | 0 ms |

The no-response candidate begins after the firmware velocity observation settles;
the measured 530 ms injection response includes observation phase plus the full
500 ms qualification window. The heartbeat remains fresh through 150 ms and is
detected at a subsequent 20 ms Safety release. Detection-to-zero is the same
detecting critical section, zero intervening control cycles. These are simulated
controller-cycle results, **not hardware interrupt or electrical shutdown latency**.

## Regression and scenarios

Before behavior edits, both Debug and Release passed the original 57/57 tests.
The completed suite contains 75 tests: 57 prior entries and 18 additions.

Final validation on native Windows / MSYS2 UCRT64 GCC 16.1.0, CMake 4.4.0 and
Ninja 1.13.2:

| Check | Result |
| --- | --- |
| Debug | 75/75 passed, 67.51 seconds |
| Release | 75/75 passed, 65.99 seconds |
| HAL isolation | Passed in both configurations, including the new safety directory |
| Ordinary control replay | Passed in both configurations |
| Nine-scenario safety replay | Passed in both configurations |
| Compiler warnings | Zero in final builds, warnings treated as errors |
| Documented Debug demo commands | 15/15 passed |

The demos were nominal, peripherals, burst, rx-overflow, diagnostic-loss, uart-long,
uart-tx-failure, plant-open-loop, plant-limits, closed-loop, estop, stall,
encoder-failure, watchdog and limit-fault. Reviewable local outputs are saved as
`build/phase6-demo-<scenario>.log`; these generated logs can be regenerated from the
documented scripts. CTest evidence is in each build's `Testing/Temporary/LastTest.log`.
Release scenarios are covered by CTest; the script-based demo sweep used Debug.

- Seven unit cases: PWM/velocity/time boundaries, transient and nonmoving exclusion,
  retained reason and two-count recovery, multiple hazards, unsigned time wrap,
  INTERNAL restart-only latch, and final gate/stale-request behavior.
- Ten real-task scenarios: moving E-stop, idle E-stop, E-stop with failed UART
  telemetry, stall, encoder failure, positive limit, negative limit, and dropped
  Motion/Safety/Communications heartbeats. Each recoverable fault scenario exercises
  rejection, invalid RESET, cleared-condition latch retention, explicit recovery,
  zero-output waiting and a successful new closed-loop move.
- One replay test runs nine safety scenarios twice in independent processes and
  compares their deterministic result, timing and recovery-move metric output.

All prior PID, controller, plant, peripheral, command/health, FreeRTOS kernel,
queue/interrupt, UART failure, HAL-isolation and control-replay entries are retained.
Intentional prior assertion changes:

1. The manual peripheral demo now expects ESTOP because all injected safety inputs
   remain active and RESET must be rejected. Encoder/GPIO/queue checks remain.
2. Healthy RTOS runs now require watchdog refreshes instead of requiring zero.
3. The old policy case no longer expects ESTOP to return NOT_IMPLEMENTED; full
   software ESTOP semantics are tested by safety scenarios, including during motion.

## Normal control comparison

The sequence's metrics and sampled output/state hashes exactly match Phase 5:

| Start -> target | Final | Error / overshoot | Completion | Settling | Hash |
| --- | ---: | --- | ---: | ---: | --- |
| 1000 -> 2000 | 2001 | 1 / 1 count | 4350 ms | 4830 ms | `1ae531fcbaecb861` |
| 2001 -> 500 | 499 | 1 / 1 count | 5600 ms | 6140 ms | `936a833ae277ea72` |
| 499 -> 3500 | 3501 | 1 / 1 count | 9350 ms | 9830 ms | `d6441e4503357f8c` |
| 3501 -> 3001 | 3000 | 1 / 1 count | 3090 ms | 3690 ms | `4f58538e2c6ccf8b` |

Peak magnitude remains 0.8. Metrics retain the two-second post-completion observation
and honest late one-count coasting behavior explained in [control](control.md).

## Files

Created:

- `firmware/safety/fault_manager.c`, `fault_manager.h`
- `firmware/safety/safety_manager.c`, `safety_manager.h`
- `firmware/rtos/safety_runtime.c`
- `tests/test_safety.c`
- `cmake/CheckSafetyReplay.cmake`
- `docs/safety.md`, `docs/phase-6.md`

Modified:

- `CMakeLists.txt`, `cmake/CheckHalIsolation.cmake`, `scripts/demo.ps1`
- `firmware/app/application.c`, `application.h`
- `firmware/drivers/motor.h`, `watchdog.h`
- `firmware/platform/host/motor_host.c`, `telemetry_host.c`
- `firmware/rtos/runtime.c`, `runtime.h`, `runtime_internal.h`
- `firmware/tasks/motion_task.c`, `safety_task.c`
- `simulator/plant_model.c`, `plant_model.h`, `closed_loop_main.c`, `main.c`
- `README.md`, `docs/architecture.md`, `docs/real-time-design.md`,
  `docs/control.md`, `docs/simulation.md`

## Explicit scope declaration

| Feature | Result |
| --- | --- |
| Full ESTOP firmware behavior | YES |
| Limit safety | YES |
| Stall detection | YES, shared no-response classification |
| Encoder-failure handling | YES for frozen feedback; shared classification |
| Watchdog supervision | YES, Option A with stated supervisor-hang limitation |
| Homing | NO |
| Physical STM32 | NO |
| Physical motor/encoder | NO |
| ARM Cortex-M build | NO |
| Renode validation | NO |
| Certified functional safety | NO |

> This project validates firmware safety behavior using deterministic host simulation.
> It does not implement or claim certified machine safety, safety-rated hardware,
> Safe Torque Off, safety relay functionality, or compliance with industrial
> functional-safety standards.

Phase 7 remains homing: a deliberate homing state machine, reference establishment,
switch approach/release policy and fault interaction. No homing, STM32 HAL, second
axis, CAN, SPI or I2C work was added. Linux/remote CI and MCU timing are unvalidated.
See [safety design](safety.md) for complete semantics and hardware limitations.
