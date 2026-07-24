# C++ parity engine + replayer (CPP_PORT_PLAN.md milestones 1-3).
#
# Build (from a Developer prompt or after vcvars):
#   cmake -S cpp/core -B cpp/build -G Ninja
#   cmake --build cpp/build
#   ctest --test-dir cpp/build --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(core CXX)

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

# The physics engine itself: Vec3/TrackCore/Track/Ship/Zone/Trigger/Simulation,
# built once as a static library so anything embedding the engine (the parity
# replayer today, a future native game host) links `core` instead of recompiling
# its translation units.
set(CORE_HEADERS
  include/Vec3.hpp
  include/TrackCore.hpp
  include/Track.hpp
  include/Ship.hpp
  include/Simulation.hpp)

add_library(core STATIC
  src/Vec3.cpp
  src/TrackCore.cpp
  src/Track.cpp
  src/Simulation.cpp
  ${CORE_HEADERS})
target_include_directories(core PUBLIC include)

# Headers carry no build step of their own, but listing them as sources (above)
# surfaces them in IDE project views; group them under their own filter there
# rather than mixed in with the .cpp files.
source_group("Header Files" FILES ${CORE_HEADERS})

add_executable(parity tests/parity_main.cpp)
target_include_directories(parity PRIVATE third_party)
target_link_libraries(parity PRIVATE core)

enable_testing()
set(TRACE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../test/traces)
add_test(NAME parity
  COMMAND parity
    ${TRACE_DIR}/starter-circle.json
    ${TRACE_DIR}/open-curve.json
    ${TRACE_DIR}/boost-circuit.json
    ${TRACE_DIR}/recovery-run.json)
