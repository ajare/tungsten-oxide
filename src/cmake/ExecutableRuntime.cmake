include_guard(GLOBAL)

get_filename_component(_tox_executable_runtime_dir
  "${CMAKE_CURRENT_LIST_DIR}/../runtime" ABSOLUTE)

if (NOT TARGET tox_executable_runtime)
  add_library(tox_executable_runtime OBJECT
    "${_tox_executable_runtime_dir}/src/ExecutableRuntime.cpp")
  target_compile_features(tox_executable_runtime PUBLIC cxx_std_20)
  target_include_directories(tox_executable_runtime PUBLIC
    "${_tox_executable_runtime_dir}/include")
endif()

# Adds the process-wide no-dialog fatal-error policy to an executable. Using an object library (not
# a static archive) guarantees that its automatic installer is retained even when the executable
# does not call the reporting API directly.
function(tox_configure_executable target)
  target_sources(${target} PRIVATE $<TARGET_OBJECTS:tox_executable_runtime>)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_include_directories(${target} PRIVATE "${_tox_executable_runtime_dir}/include")
endfunction()
