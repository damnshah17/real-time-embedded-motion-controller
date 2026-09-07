cmake_minimum_required(VERSION 3.20)
project(motion_controller VERSION 0.8.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(MOTION_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)

add_library(project_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project_options INTERFACE -Wall -Wextra -Wpedantic)
    if(MOTION_WARNINGS_AS_ERRORS)
        target_compile_options(project_options INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(project_options INTERFACE /W4)
    if(MOTION_WARNINGS_AS_ERRORS)
        target_compile_options(project_options INTERFACE /WX)
    endif()
endif()

add_library(motion_app STATIC firmware/app/application.c)
target_include_directories(motion_app PUBLIC firmware)
target_link_libraries(motion_app PRIVATE project_options)

add_library(motion_control STATIC firmware/control/pid.c firmware/control/motion_controller.c firmware/control/homing.c)
target_include_directories(motion_control PUBLIC firmware)
target_link_libraries(motion_control PRIVATE project_options)

add_library(platform_host STATIC firmware/platform/host/platform_host.c
    firmware/platform/host/host_access.c firmware/platform/host/motor_host.c
    firmware/platform/host/encoder_host.c firmware/platform/host/gpio_host.c
    firmware/platform/host/timer_host.c firmware/platform/host/watchdog_host.c
    firmware/platform/host/uart_host.c)
target_include_directories(platform_host PUBLIC firmware)
target_link_libraries(platform_host PRIVATE project_options)

add_library(plant_model STATIC simulator/plant_model.c)
target_include_directories(plant_model PUBLIC simulator)
target_link_libraries(plant_model PUBLIC platform_host PRIVATE project_options)
if(UNIX)
    target_link_libraries(plant_model PUBLIC m)
endif()

add_executable(motion_controller_plant simulator/plant_main.c)
target_link_libraries(motion_controller_plant PRIVATE plant_model project_options)

include(cmake/FreeRTOS.cmake)

add_library(firmware_support STATIC firmware/protocol/command.c firmware/health/task_health.c
    firmware/safety/fault_manager.c firmware/safety/safety_manager.c)
target_include_directories(firmware_support PUBLIC firmware)
target_link_libraries(firmware_support PRIVATE project_options)

add_library(motion_rtos STATIC firmware/rtos/runtime.c firmware/rtos/safety_runtime.c firmware/rtos/periodic.c firmware/rtos/command_bus.c
    firmware/rtos/diagnostics.c firmware/tasks/motion_task.c firmware/tasks/safety_task.c
    firmware/tasks/comms_task.c firmware/tasks/telemetry_task.c)
target_link_libraries(motion_rtos PUBLIC freertos_kernel motion_app firmware_support motion_control PRIVATE project_options)

add_library(platform_host_rtos STATIC firmware/platform/host/input_host.c firmware/platform/host/telemetry_host.c)
target_link_libraries(platform_host_rtos PUBLIC freertos_kernel platform_host firmware_support PRIVATE project_options)

add_executable(motion_controller_host simulator/main.c)
target_link_libraries(motion_controller_host PRIVATE motion_rtos platform_host_rtos project_options)

add_executable(motion_controller_closed_loop simulator/closed_loop_main.c simulator/control_metrics.c)
target_link_libraries(motion_controller_closed_loop PRIVATE motion_rtos platform_host_rtos plant_model project_options)

add_executable(motion_controller_startup simulator/startup_main.c)
target_link_libraries(motion_controller_startup PRIVATE motion_app platform_host project_options)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_telemetry tests/test_telemetry.c)
    target_link_libraries(test_telemetry PRIVATE platform_host_rtos motion_control motion_app project_options)
    add_test(NAME protocol_telemetry COMMAND test_telemetry)
    add_test(NAME protocol_roundtrip COMMAND ${CMAKE_COMMAND}
        -DRUNNER=$<TARGET_FILE:motion_controller_host> -P ${PROJECT_SOURCE_DIR}/cmake/CheckProtocol.cmake)
    set_tests_properties(protocol_roundtrip PROPERTIES TIMEOUT 15)
    add_test(NAME hal_isolation_guard COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
        -DFIXTURE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/hal-fixtures -P ${PROJECT_SOURCE_DIR}/cmake/TestHalIsolation.cmake)
    add_executable(test_homing tests/test_homing.c)
    target_link_libraries(test_homing PRIVATE motion_control firmware_support platform_host project_options)
    foreach(case reference already timeout abort states safety departure)
        add_test(NAME homing_unit_${case} COMMAND test_homing ${case})
    endforeach()
    foreach(scenario homing homing-500 homing-4000 homing-reference homing-already homing-after-move
                     homing-departure homing-stop homing-estop homing-software-estop homing-positive-limit
                     homing-watchdog homing-stall homing-encoder-failure homing-timeout)
        add_test(NAME ${scenario} COMMAND motion_controller_closed_loop --quiet --scenario ${scenario})
        set_tests_properties(${scenario} PROPERTIES TIMEOUT 30)
    endforeach()
    add_test(NAME homing_replay COMMAND ${CMAKE_COMMAND}
        -DRUNNER=$<TARGET_FILE:motion_controller_closed_loop> -P ${PROJECT_SOURCE_DIR}/cmake/CheckHomingReplay.cmake)
    set_tests_properties(homing_replay PROPERTIES TIMEOUT 55)
    add_executable(test_safety tests/test_safety.c)
    target_link_libraries(test_safety PRIVATE firmware_support platform_host project_options)
    foreach(case threshold transient retention multiple wrap internal gate)
        add_test(NAME safety_unit_${case} COMMAND test_safety ${case})
    endforeach()
    add_executable(test_pid tests/test_pid.c)
    target_link_libraries(test_pid PRIVATE motion_control project_options)
    foreach(case zero proportional integral derivative saturation windup reset determinism invalid)
        add_test(NAME pid_${case} COMMAND test_pid ${case})
    endforeach()
    add_executable(test_motion tests/test_motion.c)
    target_link_libraries(test_motion PRIVATE motion_control platform_host project_options)
    foreach(case stability velocity reset invalid)
        add_test(NAME motion_${case} COMMAND test_motion ${case})
    endforeach()
    add_executable(test_control_metrics tests/test_control_metrics.c simulator/control_metrics.c)
    target_include_directories(test_control_metrics PRIVATE simulator)
    target_link_libraries(test_control_metrics PRIVATE motion_control platform_host project_options)
    add_test(NAME control_metrics COMMAND test_control_metrics)
    add_test(NAME closed_loop_replay COMMAND ${CMAKE_COMMAND}
        -DRUNNER=$<TARGET_FILE:motion_controller_closed_loop> -P ${PROJECT_SOURCE_DIR}/cmake/CheckControlReplay.cmake)
    set_tests_properties(closed_loop_replay PROPERTIES TIMEOUT 55)
    foreach(scenario positive negative relative sequence zero policy stop)
        add_test(NAME closed_loop_${scenario} COMMAND motion_controller_closed_loop --quiet --scenario ${scenario})
        set_tests_properties(closed_loop_${scenario} PROPERTIES TIMEOUT 30)
    endforeach()
    foreach(scenario estop estop-idle estop-telemetry stall encoder-failure limit-fault negative-limit watchdog watchdog-safety watchdog-comms)
        add_test(NAME safety_${scenario} COMMAND motion_controller_closed_loop --quiet --scenario ${scenario})
        set_tests_properties(safety_${scenario} PROPERTIES TIMEOUT 30)
    endforeach()
    add_test(NAME safety_replay COMMAND ${CMAKE_COMMAND}
        -DRUNNER=$<TARGET_FILE:motion_controller_closed_loop> -P ${PROJECT_SOURCE_DIR}/cmake/CheckSafetyReplay.cmake)
    set_tests_properties(safety_replay PROPERTIES TIMEOUT 55)
    add_executable(test_plant tests/test_plant.c)
    target_link_libraries(test_plant PRIVATE plant_model project_options)
    foreach(case zero positive negative magnitude damping velocity positive_limit negative_limit
                 encoder reference determinism reset invalid ownership)
        add_test(NAME plant_${case} COMMAND test_plant ${case})
    endforeach()
    foreach(scenario open-loop limits)
        add_test(NAME plant_demo_${scenario} COMMAND motion_controller_plant --quiet --scenario ${scenario})
        set_tests_properties(plant_demo_${scenario} PROPERTIES TIMEOUT 5)
    endforeach()
    add_executable(test_peripherals tests/test_peripherals.c)
    target_link_libraries(test_peripherals PRIVATE platform_host project_options)
    foreach(peripheral motor encoder gpio timer watchdog)
        add_test(NAME peripheral_${peripheral} COMMAND test_peripherals ${peripheral})
    endforeach()
    add_executable(test_uart tests/test_uart.c)
    target_link_libraries(test_uart PRIVATE platform_host project_options)
    add_test(NAME peripheral_uart COMMAND test_uart)
    add_test(NAME hal_isolation COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckHalIsolation.cmake)
    add_executable(test_kernel tests/test_kernel.c)
    target_link_libraries(test_kernel PRIVATE motion_rtos project_options)
    add_test(NAME kernel_execution COMMAND test_kernel)
    set_tests_properties(kernel_execution PROPERTIES TIMEOUT 10)
    foreach(unit command health)
        add_executable(test_${unit} tests/test_${unit}.c)
        target_link_libraries(test_${unit} PRIVATE firmware_support project_options)
        add_test(NAME ${unit}_logic COMMAND test_${unit})
    endforeach()
    add_executable(test_startup tests/test_startup.c)
    target_link_libraries(test_startup PRIVATE motion_app project_options)
    add_test(NAME application_startup COMMAND test_startup)
    add_test(NAME host_smoke COMMAND motion_controller_startup)
    set_tests_properties(host_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "PHASE1_OK state=IDLE logical_ms=2 motor_enabled=0"
        TIMEOUT 5)
    foreach(scenario nominal burst rx-overflow diagnostic-loss peripherals uart-long uart-tx-failure)
        add_test(NAME rtos_${scenario} COMMAND motion_controller_host --quiet --scenario ${scenario})
        set_tests_properties(rtos_${scenario} PROPERTIES TIMEOUT 20)
    endforeach()
endif()
