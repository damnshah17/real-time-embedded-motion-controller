# Phase 1 implementation and validation

> Historical phase report. Behavior and test counts below describe Phase 1 only.
> See [Phase 8](phase-8.md) and [protocol](protocol.md) for the current host baseline.

## Scope and inspection

The workspace was empty; there was no existing source or Git repository to modify.
Implemented Phase 1 only. No commits, pushes, ARM build or Renode work were performed.

The initial execution mechanism is the explicitly permitted appropriate host path:
a finite deterministic runner calling portable application startup steps. Genuine
FreeRTOS integration remains Phase 2 work. No imitation FreeRTOS APIs are included.

## Dependencies

Validated on Windows with PowerShell, MSYS2 UCRT64 GCC 16.1.0, CMake 4.4.0 and
Ninja 1.13.2 already installed. Minimum CMake version is 3.20. CTest is bundled
with CMake. No new packages or runtime libraries were downloaded. Host binaries
depend on the selected compiler's normal C runtime; standalone redistribution
without that toolchain has not been validated. Project source uses the MIT license.

## Commands and checks

From the repository root:

```powershell
./scripts/test.ps1 -Configuration Release
./scripts/test.ps1
./scripts/demo.ps1
```

The test script invokes the build script, which runs:

```powershell
cmake -S . -B build/host-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-Release
ctest --test-dir build/host-Release --output-on-failure
```

For Debug it substitutes `host-Debug` and `-DCMAKE_BUILD_TYPE=Debug`.
Scripts pass absolute paths so a workspace path containing spaces works.

Debug and Release compiled the application library, host platform library, host executable
and independent application-test executable. Both CTest cases passed:

1. `application_startup`: startup state sequence, transition timestamps, stable
   IDLE, output inhibition and independence from failed diagnostic output.
2. `host_smoke`: real linked host executable exits successfully and reports IDLE,
   logical time 2 ms and disabled motor output. CTest enforces a five-second timeout.

Compiler flags include `-Wall -Wextra -Wpedantic -Werror`; no project warnings or
compile errors were reported. Checks remain active in Release, without relying
on the standard `assert` macro. No control-performance metrics exist in Phase 1.

The first sandboxed Debug attempt stalled during CMake's compiler ABI check.
The stalled CMake/Ninja processes were stopped, and the Release build/test run
succeeded outside the sandbox. The default Debug build and both tests then also
passed outside the sandbox. `./scripts/demo.ps1` ran successfully and printed:

```text
[000000] SYSTEM state=BOOT
[000001] STATE BOOT -> INITIALIZING
[000002] STATE INITIALIZING -> IDLE
PHASE1_OK state=IDLE logical_ms=2 motor_enabled=0
```

This was an environment execution issue, not a source-code fix. GitHub CI is
configured but has not been executed remotely.

## Files created

- Root: `CMakeLists.txt`, `README.md`, `LICENSE`, `.gitignore`.
- Application: `firmware/app/application.c`, `firmware/app/application.h`.
- HAL: `firmware/drivers/platform.h`.
- Host: `firmware/platform/host/platform_host.c`, `firmware/platform/host/platform_host.h`.
- Runner and test: `simulator/main.c`, `tests/test_startup.c`.
- Scripts: `scripts/build.ps1`, `scripts/test.ps1`, `scripts/demo.ps1`.
- CI: `.github/workflows/ci.yml`.
- Documentation: `docs/architecture.md`, `docs/real-time-design.md`, `docs/phase-1.md`.

Build products are generated under ignored `build/` directories. No pre-existing
files were modified. Documentation and source created during this phase were
refined during validation.

## Limitations and Phase 2 handoff

There is no scheduler, ISR, command queue, UART receive, motor plant, encoder, PID,
homing, fault management or watchdog. The output-disable flag is a startup model,
not electrical safety. Logical time is not measured host scheduling time. The
complete project brief is not yet satisfied, by design of its Phase 1 instruction.

Phase 2 should select a genuine FreeRTOS host execution option, then implement
meaningful task ownership, priorities, a bounded typed command queue, justified
event signaling, heartbeats and scheduling validation. The RTOS kernel and port
must be pinned and their license recorded when introduced. Add peripheral behavior
and controls only in the subsequent phases specified by the brief.
