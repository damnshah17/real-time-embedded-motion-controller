# Disposable fixtures stay under the caller's build directory; real firmware is untouched.
set(cases "#include <Windows.h>" "#include \"platform/host/gpio_host.h\""
    "#include \"simulator/plant_model.h\"" "void bad(void) { Sleep(1)\; }")
set(index 0)
foreach(content IN LISTS cases)
    math(EXPR index "${index} + 1")
    set(root "${FIXTURE_ROOT}/${index}")
    file(MAKE_DIRECTORY "${root}/firmware/control")
    file(WRITE "${root}/firmware/control/probe.c" "${content}\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${root}"
        -P "${SOURCE_ROOT}/cmake/CheckHalIsolation.cmake" RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
    if(result EQUAL 0)
        message(FATAL_ERROR "Isolation guard accepted forbidden fixture ${index}")
    endif()
endforeach()
file(WRITE "${FIXTURE_ROOT}/safe/firmware/control/probe.c" "#include \"drivers/gpio.h\"\nvoid safe(void) { xTaskGetTickCount(); }\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${FIXTURE_ROOT}/safe"
    -P "${SOURCE_ROOT}/cmake/CheckHalIsolation.cmake" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Isolation guard rejected portable fixture")
endif()
