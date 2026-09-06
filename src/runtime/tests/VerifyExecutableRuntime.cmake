if (NOT DEFINED EXECUTABLE)
  message(FATAL_ERROR "EXECUTABLE is required")
endif()

get_filename_component(_directory "${EXECUTABLE}" DIRECTORY)
get_filename_component(_name "${EXECUTABLE}" NAME_WE)
set(_log "${_directory}/${_name}.log")
file(REMOVE "${_log}")

function(run_failure mode expected_message)
  execute_process(
    COMMAND "${EXECUTABLE}" "${mode}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 5)
  if (_result STREQUAL "0")
    message(FATAL_ERROR "${mode} unexpectedly exited successfully")
  endif()
  if (_result MATCHES "timeout")
    message(FATAL_ERROR "${mode} blocked instead of exiting: ${_result}")
  endif()
  if (NOT EXISTS "${_log}")
    message(FATAL_ERROR "${mode} did not create ${_log}")
  endif()
  file(READ "${_log}" _contents)
  string(FIND "${_contents}" "${expected_message}" _position)
  if (_position EQUAL -1)
    message(FATAL_ERROR
      "${mode} did not log '${expected_message}'.\nstdout: ${_stdout}\nstderr: ${_stderr}\nlog: ${_contents}")
  endif()
endfunction()

run_failure("--throw" "Unhandled exception: runtime probe exception")
run_failure("--abort" "Process aborted (SIGABRT)")
if (TEST_SEH)
  run_failure("--seh" "Unhandled Windows exception 0xC0000005")
endif()
file(REMOVE "${_log}")
