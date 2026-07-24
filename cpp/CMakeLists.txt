# C++ parity engine + replayer (CPP_PORT_PLAN.md milestones 1-3).
#
# Build (from a Developer prompt or after vcvars):
#   cmake -S cpp -B cpp/build -G Ninja
#   cmake --build cpp/build
#   ctest --test-dir cpp/build --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(tungsten_oxide_parity CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
  # /fp:precise (MSVC default) stops FMA contraction fusing a*b+c that the JS
  # never used — keeps per-step error small so the tolerance stays easy to pick.
  add_compile_options(/fp:precise /W3 /EHsc)
else()
  add_compile_options(-ffp-contract=off -Wall -Wextra)
endif()

add_executable(parity tests/parity_main.cpp)
target_include_directories(parity PRIVATE include third_party)

enable_testing()
set(TRACE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../test/traces)
add_test(NAME parity
  COMMAND parity
    ${TRACE_DIR}/starter-circle.json
    ${TRACE_DIR}/open-curve.json
    ${TRACE_DIR}/boost-circuit.json
    ${TRACE_DIR}/recovery-run.json)
