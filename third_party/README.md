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
Unused heap implementations, embedded ports and demos are not vendored.

Windows links the system `winmm` library. Linux links POSIX threads and defines
`_POSIX_C_SOURCE=200809L` for the upstream helper's APIs under strict C11.
No network access is needed for a clean checkout build. `.gitattributes` preserves
the original upstream file bytes for hash verification on Windows and Linux.

To audit provenance, extract the recorded official archive and compare the
manifest paths against that release. To upgrade intentionally, review the new
kernel and ports, replace the selected upstream files, regenerate the manifest,
record the new release/archive hash and rerun every host test.
