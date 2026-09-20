# Phase 8 — Host consolidation and readiness

> Historical Phase 8 report. [Phase 9](phase-9.md) now adds the ARM cross-compiled
> target; statements below about no ARM build describe the Phase 8 endpoint.

Phase 8 polishes the existing host implementation without adding a motion feature.
The controller, PID gains, plant equations, task rates, safety thresholds and
selected post-home departure policy are unchanged. No commit, push, ARM/STM32
implementation, Renode work or final resume notes were produced.

## Protocol and diagnostics

STATUS previously only said to consult periodic telemetry. It now produces an
actual runtime snapshot, sampled once at Telemetry service time. STATUS and TEL
share five labeled lines with the same timestamp:

| Group | Fields |
| --- | --- |
| motion | state, target_counts, position_counts, velocity_counts_s, error_counts, feedback_valid |
| safety | requested_pwm, applied_pwm, fault, fault_ms, estop, limit_neg, limit_pos, inputs_valid |
| health | motion_fresh, safety_fresh, comms_fresh, watchdog_supervision, refreshes |
| home | result, referenced, elapsed_ms, completed, contact_allowance |
| counters | queue_used, queue_capacity, queue_high_water, diagnostic_drops, moves_completed, aborted |

The copy is synchronized; formatting and UART output happen outside the critical
section. STATUS's acknowledgement carries `snapshot_ms`, distinguishing service
time from the command-consumption timestamp. Fields are latest published firmware
observations plus current driver requested/applied duty, not plant truth or new
simultaneous hardware samples. A safety override may precede Motion adopting the
fault, so the latest motion target/result can briefly describe the aborted operation.
Periodic telemetry remains at the existing 10 Hz rate and each write is bounded
to 256 bytes. Tests check large numeric values without truncated output/TX errors.

HELP now gives syntax and purpose for MOVE, MOVE_REL, HOME, STOP, ESTOP, RESET,
STATUS and HELP. It explicitly labels `SPEED <integer>` deferred. SPEED remains
NOT_IMPLEMENTED in ordinary healthy operation, with existing INVALID_STATE safety
and HOMING rejection taking precedence. No pretend speed-control feature was added.

The existing `ACK QUEUED` → Motion `ACK ACCEPTED`/`ERR ...` distinction remains.
Neither acknowledgement means successful motion completion. MOVE/MOVE_REL finish
with a separate completion event/counter; HOME has its own result/counter and
completion event. STOP/E-stop/fault do not report success for the abandoned motion.
Existing parser and controller error enums remain; the obsolete formatter fallback
`ERR NOT_IMPLEMENTED phase=5` was removed. Tests cover queue/acceptance wording,
syntax errors, state/range errors, deferred SPEED, HELP and actual STATUS output.

Firmware has **no SIM text parser**. Named scenarios inject E-stop through host
GPIO APIs, stall/freeze through plant flags, and dropped observations through the
heartbeat filter. `SIM ...` frames are explicitly tested as UNKNOWN_COMMAND.
Portable firmware does not read those host flags. The [protocol reference](protocol.md)
records the intentional diagnostic-format change, exact errors, syntax and units.

## Metrics command and definitions

```powershell
./scripts/metrics.ps1
./scripts/metrics.ps1 -Configuration Release
```

The script builds the selected configuration, obtains settings from the runner's
`--configuration-json` mode, executes nine existing deterministic scenarios, and
parses their emitted records. It prints human-readable results and writes
`build/metrics-<Configuration>.json`; `-JsonPath` selects another file. CTest's JSON
inventory supplies test count and `demo.ps1` parameter metadata supplies demo count.
Nothing reads old reports or substitutes hardcoded measured results.

Compiled configuration is labeled separately from measured scenarios. Position,
target, error and overshoot are counts; velocity is counts/s; duty is normalized;
all scenario durations are simulated milliseconds. A default axis unit maps to one
raw encoder count, not a physical distance. Completion, settling and the two-second
post-observation definitions remain those in [control](control.md).

Metrics include no wall-clock duration or current timestamp. Same-build JSON should
be identical across runs. Build console text and host scheduling statistics are not
part of that deterministic artifact. The script reports warning count as null with
an explanation: an incremental build cannot measure a full compiler warning count.
Configured test/demo counts do not mean the metrics command ran the whole suite.

| Compiled configuration | Value |
| --- | --- |
| Motion / Safety / Telemetry | 100 / 50 / 10 Hz |
| Command queue | 8 copied commands |
| Plant numerical updates | 1000 per simulated second |
| PID Kp / Ki / Kd | 0.006 / 0.0002 / 0.0015 |
| PID PWM limits | -0.8..+0.8 |
| Completion position / speed / dwell | 3 counts / 25 counts/s / 20 samples |
| No-motion PWM / speed / duration | 0.30 / 25 counts/s / 500 ms |
| Heartbeat freshness | 150 ms |
| Host watchdog timeout | 1000 ms |
| HOME duty / timeout | -0.30 / 40000 ms |

| Measured control scenario | Final / error / overshoot | Completion | Settling | Peak absolute PWM |
| --- | --- | ---: | ---: | ---: |
| Positive 1000 → 2000 | 2001 / 1 / 1 count | 4350 ms | 4830 ms | 0.8 |
| Negative 1000 → 500 | 499 / 1 / 1 count | 3100 ms | 3630 ms | 0.8 |

| Measured safety scenario | Input-to-detection | Detection-to-zero |
| --- | ---: | ---: |
| Moving GPIO E-stop | 10 ms | 0 ms |
| Idle GPIO E-stop | 20 ms | 0 ms |
| Mechanical stall | 530 ms | 0 ms |
| Dropped Motion heartbeat | 170 ms | 0 ms |

Zero simulated milliseconds means detection and gate enforcement occur in the same
logical-time critical section, with no intervening simulated step. It is **not**
zero physical latency. Input sampling phase still produces the measured 10/20 ms
E-stop response. Stall response includes observation delay plus the full 500 ms
qualification. Watchdog response is software heartbeat supervision, not a measured
autonomous hardware reset or proof of handling a stopped supervisor.

| Measured HOME start | HOME duration | Result |
| --- | ---: | --- |
| 500 counts | 3590 ms | COMPLETE, logical zero, PWM zero |
| 1500 counts | 10250 ms | COMPLETE, logical zero, PWM zero |
| 4000 counts | 26920 ms | COMPLETE, logical zero, PWM zero |

## Audit findings and source boundaries

The current architecture already separates `motion_app`, `motion_control`,
`firmware_support`, `motion_rtos`, `freertos_kernel`, host platform, plant and
runner targets. It did not need a broad refactor or extra libraries. The
[source inventory](stm32-port.md) identifies reusable logic, replaceable host
sources, tests, simulator code and host-port hooks/configuration.

Two functions had no callers anywhere in project code/tests:
`platform_input_receive` and `platform_input_pending`. They only wrapped UART
functions now used directly by Communications; their declarations/definitions were
removed. The host frame type alias remains in use. Startup helpers, startup runner,
manual input, tick/ISR notification path and stepped runner remain actively tested.
No other speculative dead-code removal was performed.

The HAL guard already rejected host/plant includes and calls. It now also handles
mixed-case Windows headers, backslash paths and obvious Windows API calls. Negative
fixtures reject host, simulator, Windows headers and `Sleep`. A safe fixture retains
`xTaskGetTickCount`: an initial substring false positive was corrected by requiring
a function-name boundary. This remains a small textual guard, not full static analysis.

Documentation fixes include:

- Stale STM32 porting text claiming GPIO did not cause faults.
- Real-time text omitting gate authority and GPIO safety policy.
- Old mixed telemetry-field descriptions and STATUS's periodic-snapshot placeholder.
- README's buried host limitation, dispersed results and outdated category counts.
- Historical Phase 1–7 claims that could be mistaken for current behavior: each
  report now has a historical banner; measured historical results were preserved.
- The old control introduction's ambiguous safety status and simulation's “future
  control logic” wording, now aligned with implemented control/safety.

Marker audit (project sources, scripts, docs, CMake, CI and vendored source):

| Remaining match | Classification / action |
| --- | --- |
| SPEED NOT_IMPLEMENTED enum, handler, HELP, tests and docs | Intentional deferred command; retained and explained |
| Old phase NOT_IMPLEMENTED examples | Historical snapshots, explicitly labeled |
| Obsolete-response strings in protocol regression | Negative assertions preventing reintroduction |
| Phase 3 narrative mentioning a resolved “placeholder” | Historical account, not unfinished implementation |
| Vendor newlib “stubs” comment | Upstream guidance, unchanged |
| Vendor ENOTEMPTY | Lexical TEMP substring, not a task marker |
| TODO / FIXME / HACK task markers in project code | None found |

Case-insensitive broad searches also match ordinary words such as “attempt” and
“temporary”; these are not pending work. ARM/STM32/Renode, hardware validation,
advanced homing and speed control remain intentional future scope, not cleanup
items to implement here. Vendored files remain hash-verified and untouched.

## Validation and traceability

Before behavior edits: Debug **98/98** (105.97 s), Release **98/98** (103.16 s),
and all **18/18 demos** passed. Phase 8 adds three CTest entries: formatter/STATUS
coverage, actual RTOS protocol roundtrip, and HAL guard fixtures. Parser tests also
verify rejection of SIM commands. All original 98 entries and safety/control
assertions remain. Final inventory is **101 tests / 18 demos**.

STATUS and TEL formatter tests cover IDLE, MOVING, STOPPING, HOMING, FAULT and ESTOP,
including explicit units, separate requested/applied output, heartbeat state,
single snapshot acquisition, numeric extremes, quiet mode and zero UART errors.
The real-task protocol test checks queue admission, STATUS, HELP, malformed input
and SPEED. Original real-task scenarios continue testing motion/homing commands,
busy rejection, E-stop/RESET, no-motion, watchdog and completion/abort semantics.

| Feature | Validation | Demonstration | Status |
| --- | --- | --- | --- |
| Startup / four RTOS tasks | startup, kernel_execution, rtos_* | default / nominal | Host implemented |
| Protocol / diagnostics | command_logic, protocol_telemetry, protocol_roundtrip | default | Host implemented |
| Peripheral contracts | peripheral_* and UART failure scenarios | peripherals | Host implemented |
| Plant / PID / motion | plant_*, pid_*, motion_*, closed_loop_*, replay | closed-loop | Host implemented |
| E-stop / limits / RESET | safety units and safety scenarios | estop / limit-fault | Host implemented |
| No-motion / frozen feedback | safety threshold and recovery scenarios | stall / encoder-failure | Host implemented |
| Watchdog supervision | health_logic and three dropped-heartbeat scenarios | watchdog | Host implemented |
| HOME / reference / departure | seven units, 15 scenarios, replay | homing | Host implemented |
| Source boundary | hal_isolation and hal_isolation_guard | CTest | Host checked |
| Cortex-M / STM32 / Renode | None | None | Future, not validated |

| Final serial validation | Actual result |
| --- | --- |
| `./scripts/test.ps1` | **101/101 passed**, 106.93 s |
| `./scripts/test.ps1 -Configuration Release` | **101/101 passed**, 109.96 s |
| Protocol formatter and real-task roundtrip | Passed in both configurations |
| HAL isolation and forbidden/safe fixtures | Passed in both configurations |
| Control / safety / homing replay | All passed in both configurations |
| Fresh `build/phase8-clean-Debug` configure/build | All targets built successfully |
| Clean-build selected protocol/RTOS/isolation/startup checks | **6/6 passed**, 4.34 s |
| Clean-build default peripheral scenario | Passed, latched ESTOP as expected |
| Compiler warnings | **0** in clean Debug compilation and Phase 8 Debug/Release build logs |
| `./scripts/demos.ps1` | **18/18 passed**, Debug; includes default peripheral invocation |
| Metrics command, two final Debug runs | Both succeeded; complete JSON files byte-identical |

The measured tables above were reproduced in both final metrics runs. The commands
were `./scripts/metrics.ps1 -JsonPath build/phase8-metrics-first.json` followed by
`./scripts/metrics.ps1`. SHA256 for both resulting JSON files:
`4794C1B77812DD5BDA0A62DD60021D2E973A57F61836CB1229F83829157D2F47`.
The default output is `build/metrics-Debug.json`. Demo results are in
`build/phase8-final-demos.log` and individual `build/demo-Debug-<scenario>.log` files.
All 18 scenarios in README's catalog were executed; no scenario was removed.

Commands for the clean-directory checks were:

```powershell
./scripts/build.ps1 -BuildDirectory build/phase8-clean-Debug
ctest --test-dir build/phase8-clean-Debug --output-on-failure -R 'protocol_|hal_isolation|kernel_execution|host_smoke'
./build/phase8-clean-Debug/motion_controller_host.exe --quiet --scenario peripherals
```

This validates a fresh build tree, not a Git clone or tool installation on a new
machine. The full suites run in the standard Debug/Release directories; only the
six selected checks plus default scenario were run in the clean directory.
Logs are saved as `build/phase8-final-serial-Debug.log`,
`build/phase8-final-serial-Release.log`, `build/phase8-clean-build.log` and
`build/phase8-clean-serial-tests.log`. The final serial builds follow successful
compilation of changed code; the clean Debug build compiles all targets from scratch.

The first overlapping final/clean validation attempts failed: nominal showed a
1050 ms Motion gap and FAULT, a replay process returned failure despite printing its
scenario success record, and the clean protocol runner failed. Those runs overlapped
a fresh build and native host-port execution. They are retained as evidence of host
scheduling sensitivity, not hidden by relaxed assertions. Final validation runs
serially; there is no added retry loop or weakened timing/safety threshold.
The serial full suites and clean checks above passed without further code changes.

## CI, clean build and Phase 9 readiness

GitHub Actions now retains the Windows UCRT64 Debug/Release matrix. Configure/build
and full CTest include protocol, HAL isolation and all three deterministic replays.
The locally unverified POSIX CMake path is retained but removed from the claimed CI
baseline. Remote GitHub Actions has **not** been executed; local equivalents alone
are claimed. No ARM job or new toolchain dependency was added.

Build/test scripts accept `-BuildDirectory` for fresh-directory validation and
create needed directories through CMake. Errors stop the scripts. The all-demo
script derives the catalog from the existing demo parameter rather than maintaining
a duplicate list. README recommends three coherent existing demos in fresh processes
instead of adding an artificial combined firmware feature.

The host source layout is ready to support separately authorized Phase 9 work.
It is **not** an ARM-link-ready target: Phase 9 must select a board/MCU and embedded
FreeRTOS port, provide target startup/link/platform code and adapt hooks/configuration.
Host `fprintf`/`abort` assertion behavior, port clock values, desktop stacks and
simulated tick input are explicit replacement/review obligations. No such target
code was introduced here.

Remaining limits include bounded best-effort diagnostic delivery, no request IDs,
no interactive SIM console, no speed loop, ideal plant mechanics, no hardware
watchdog reset, no hard real-time guarantees and no certified safety. Homing retains
its one-stage policy and cannot bypass an already latched limit fault. Linux, real
ISR timing, electrical PWM/encoder inputs and physical motor behavior remain unvalidated.

Physical STM32 used: **NO**. Physical motor/encoder used: **NO**.
ARM Cortex-M build: **NO**. Renode validation: **NO**.

## Files created and modified

Created:

- `scripts/metrics.ps1`, `scripts/demos.ps1`
- `tests/test_telemetry.c`
- `cmake/CheckProtocol.cmake`, `cmake/TestHalIsolation.cmake`
- `docs/protocol.md`, `docs/phase-8.md`

Modified:

- `CMakeLists.txt` (version 0.8.0 and three tests)
- `cmake/CheckHalIsolation.cmake`, `cmake/FreeRTOS.cmake`
- `.github/workflows/ci.yml`
- `scripts/build.ps1`, `scripts/test.ps1`
- `firmware/drivers/input.h`, `firmware/platform/host/input_host.c`
- `firmware/platform/host/telemetry_host.c`
- `simulator/closed_loop_main.c` (compiled configuration output only)
- `tests/test_command.c`
- `README.md`
- `docs/architecture.md`, `docs/real-time-design.md`, `docs/control.md`,
  `docs/safety.md`, `docs/homing.md`, `docs/simulation.md`, `docs/stm32-port.md`
- `docs/phase-1.md` through `docs/phase-7.md` (historical banners only)

Generated logs, JSON and clean build artifacts are under ignored `build/`.
`scripts/demo.ps1`, portable control/safety/homing behavior and FreeRTOS vendor
sources are unchanged. No new firmware task or application allocation was added.

## Completion declaration

| Required status | Result |
| --- | --- |
| Host implementation consolidated | YES |
| STATUS complete | YES, with documented service-time snapshot semantics |
| Metrics reproducible | YES, final Debug JSON identical across two runs |
| Current host CI configured honestly | YES, Windows Debug/Release; remote execution unverified |
| Physical STM32 used | NO |
| Physical motor/encoder used | NO |
| ARM Cortex-M build | NO |
| Renode validation | NO |

Phase 8 is complete. The host baseline is ready for separately scoped Phase 9
target-build work, with the source/configuration obligations above. No commit or
push occurred. Work stops after Phase 8.
