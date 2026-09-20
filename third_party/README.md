# FreeRTOS Kernel dependency

This directory vendors an unmodified subset of the official **FreeRTOS Kernel
V11.2.0** release, under its included MIT license. It is a pinned version, not a
claim to track the latest release.

- Source: https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.2.0
- Download: https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V11.2.0.zip
- Download SHA256: `faeabe1e7443208d83232870a6072ec72982ddd219a8dfa74eda8c1eadb97819`
- Per-file SHA256 manifest: `FreeRTOS-Kernel.sha256` (verified during CMake configuration).
- License: `FreeRTOS-Kernel/LICENSE.md`.

Included: `tasks.c`, `queue.c`, `list.c`, public headers, the official
`portable/MSVC-MingW` port and `portable/ThirdParty/GCC/Posix` port including its
event helper. No kernel source edits or patches were made. The project-owned
`cmake/FreeRTOS.cmake` compiles the required source subset as `freertos_kernel`.
Unused heap implementations and demos are not vendored. Phase 9 adds the official
unmodified `portable/GCC/ARM_CM4F` port at the same V11.2.0 tag; its two files are
also covered by `FreeRTOS-Kernel.sha256`.

Windows links the system `winmm` library. Linux links POSIX threads and defines
`_POSIX_C_SOURCE=200809L` for the upstream helper's APIs under strict C11.
No network access is needed for a clean checkout build. `.gitattributes` preserves
the original upstream file bytes for hash verification on Windows and Linux.

To audit provenance, extract the recorded official archive and compare the
manifest paths against that release. To upgrade intentionally, review the new
kernel and ports, replace the selected upstream files, regenerate the manifest,
record the new release/archive hash and rerun every host test.

## ARM support subset (Phase 9)

- `CMSIS/`: ARM CMSIS_5 **5.9.0**, Core/Include headers `core_cm4.h`,
  `cmsis_version.h`, `cmsis_compiler.h`, `cmsis_gcc.h`, `mpu_armv7.h`.
  Source: https://github.com/ARM-software/CMSIS_5/tree/5.9.0/CMSIS/Core/Include
  Apache-2.0 license retained at `CMSIS/LICENSE.txt`.
- `STM32F4/`: ST cmsis-device-f4 **v2.6.10**, `stm32f407xx.h`,
  `system_stm32f4xx.h`, and the original GCC startup assembly retained for vector
  provenance (not compiled). Source:
  https://github.com/STMicroelectronics/cmsis-device-f4/tree/v2.6.10
  Apache-2.0 license retained at `STM32F4/LICENSE.md`.
- Files downloaded unchanged from those tagged upstream paths. Per-file hashes in
  `arm-support.sha256` are checked on ARM configuration. The project startup
  documents its modifications separately. No STM32Cube HAL dependency is used.
