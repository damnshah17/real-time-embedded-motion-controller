if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    message(FATAL_ERROR "STM32 requires cmake/toolchains/arm-none-eabi.cmake")
endif()
enable_language(ASM)
message(STATUS "ARM toolchain: ${CMAKE_TOOLCHAIN_FILE}")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(kernel_dir "${PROJECT_SOURCE_DIR}/third_party/FreeRTOS-Kernel")
file(STRINGS "${PROJECT_SOURCE_DIR}/third_party/FreeRTOS-Kernel.sha256" hashes)
foreach(entry IN LISTS hashes)
    string(SUBSTRING "${entry}" 0 64 expected)
    string(SUBSTRING "${entry}" 66 -1 path)
    file(SHA256 "${kernel_dir}/${path}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "FreeRTOS integrity mismatch: ${path}")
    endif()
endforeach()
file(STRINGS "${PROJECT_SOURCE_DIR}/third_party/arm-support.sha256" arm_hashes)
foreach(entry IN LISTS arm_hashes)
    string(SUBSTRING "${entry}" 0 64 expected)
    string(SUBSTRING "${entry}" 66 -1 path)
    file(SHA256 "${PROJECT_SOURCE_DIR}/third_party/${path}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "ARM support integrity mismatch: ${path}")
    endif()
endforeach()
set(port "${kernel_dir}/portable/GCC/ARM_CM4F")
add_library(freertos_kernel STATIC "${kernel_dir}/tasks.c" "${kernel_dir}/queue.c"
    "${kernel_dir}/list.c" "${port}/port.c")
target_include_directories(freertos_kernel PUBLIC firmware/platform/stm32 firmware
    "${kernel_dir}/include" "${port}")
target_link_libraries(freertos_kernel PRIVATE project_options)

add_executable(motion_controller_stm32
    firmware/app/application.c
    firmware/control/pid.c firmware/control/motion_controller.c firmware/control/homing.c
    firmware/protocol/command.c firmware/health/task_health.c
    firmware/safety/fault_manager.c firmware/safety/safety_manager.c
    firmware/rtos/runtime.c firmware/rtos/safety_runtime.c firmware/rtos/periodic.c
    firmware/rtos/command_bus.c firmware/rtos/diagnostics.c
    firmware/tasks/motion_task.c firmware/tasks/safety_task.c
    firmware/tasks/comms_task.c firmware/tasks/telemetry_task.c
    firmware/platform/stm32/startup.S firmware/platform/stm32/system.c
    firmware/platform/stm32/main.c firmware/platform/stm32/platform.c
    firmware/platform/stm32/motor.c firmware/platform/stm32/encoder.c
    firmware/platform/stm32/peripherals.c firmware/platform/stm32/uart.c
    firmware/platform/stm32/telemetry.c firmware/platform/stm32/tiny_format.c)
target_include_directories(motion_controller_stm32 SYSTEM PRIVATE
    third_party/CMSIS/Include third_party/STM32F4/Include)
target_link_libraries(motion_controller_stm32 PRIVATE freertos_kernel project_options m)
set_target_properties(motion_controller_stm32 PROPERTIES SUFFIX ".elf")
set(linker "${PROJECT_SOURCE_DIR}/firmware/platform/stm32/stm32f407vg.ld")
set_property(TARGET motion_controller_stm32 APPEND PROPERTY LINK_DEPENDS "${linker}")
target_link_options(motion_controller_stm32 PRIVATE -nostartfiles
    "-T${linker}" -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/motion_controller_stm32.map")
find_program(ARM_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(ARM_SIZE arm-none-eabi-size REQUIRED)
add_custom_command(TARGET motion_controller_stm32 POST_BUILD
    COMMAND "${ARM_OBJCOPY}" -O binary $<TARGET_FILE:motion_controller_stm32>
        "${CMAKE_CURRENT_BINARY_DIR}/motion_controller_stm32.bin"
    COMMAND "${ARM_SIZE}" $<TARGET_FILE:motion_controller_stm32>
    VERBATIM)
