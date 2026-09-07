execute_process(COMMAND "${RUNNER}" --scenario nominal RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Protocol runner failed: ${result} ${error}")
endif()
foreach(expected "ACK QUEUED command=STATUS" "ACK STATUS snapshot_ms=" "STATUS t_ms="
    "position_counts=" "requested_pwm=" "applied_pwm=" "watchdog_supervision="
    "HELP MOVE <position_counts>" "HELP HOME :" "deferred, NOT_IMPLEMENTED"
    "command=SPEED value=500 ERR NOT_IMPLEMENTED" "ERR INVALID_ARGUMENT" "PHASE3_OK")
    string(FIND "${output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Missing protocol output: ${expected}\n${output}")
    endif()
endforeach()
if(output MATCHES "see periodic snapshot|NOT_IMPLEMENTED phase=5")
    message(FATAL_ERROR "Obsolete protocol response")
endif()
