set(kernel_dir "${PROJECT_SOURCE_DIR}/third_party/FreeRTOS-Kernel")
file(STRINGS "${PROJECT_SOURCE_DIR}/third_party/FreeRTOS-Kernel.sha256" kernel_hashes)
foreach(entry IN LISTS kernel_hashes)
    string(SUBSTRING "${entry}" 0 64 expected_hash)
    string(SUBSTRING "${entry}" 66 -1 relative_path)
    file(SHA256 "${kernel_dir}/${relative_path}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "Vendored FreeRTOS integrity mismatch: ${relative_path}")
    endif()
endforeach()
if(WIN32)
    set(kernel_port "${kernel_dir}/portable/MSVC-MingW")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(kernel_port "${kernel_dir}/portable/ThirdParty/GCC/Posix")
else()
    message(FATAL_ERROR "Host builds support Windows and the unvalidated Linux POSIX path only.")
endif()

add_library(freertos_kernel STATIC
    "${kernel_dir}/tasks.c" "${kernel_dir}/queue.c" "${kernel_dir}/list.c"
    "${kernel_port}/port.c" "${PROJECT_SOURCE_DIR}/firmware/rtos/hooks.c")
target_include_directories(freertos_kernel PUBLIC
    "${kernel_dir}/include" "${kernel_port}"
    "${PROJECT_SOURCE_DIR}/firmware/rtos" "${PROJECT_SOURCE_DIR}/firmware")
target_link_libraries(freertos_kernel PRIVATE project_options)
if(WIN32)
    target_link_libraries(freertos_kernel PUBLIC winmm)
else()
    find_package(Threads REQUIRED)
    # Expose POSIX APIs used by the upstream event helper in strict C11 mode.
    target_compile_definitions(freertos_kernel PRIVATE _POSIX_C_SOURCE=200809L)
    target_sources(freertos_kernel PRIVATE "${kernel_port}/utils/wait_for_event.c")
    target_link_libraries(freertos_kernel PUBLIC Threads::Threads)
endif()
