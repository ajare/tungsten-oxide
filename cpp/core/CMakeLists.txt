# C++ native track engine + parity replayer.
#
# Build (from a Developer prompt or after vcvars):
#   cmake -S cpp/core -B cpp/build -G Ninja
#   cmake --build cpp/build
#   ctest --test-dir cpp/build --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(core CXX)

# A standalone core configure still consumes the repository-pinned Willpower
# sources; the combined cpp/ configure has already created this target.
if (NOT TARGET Willpower.Geometry)
  add_subdirectory(../willpower ${CMAKE_CURRENT_BINARY_DIR}/willpower)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
  # /fp:precise (MSVC default) stops FMA contraction fusing a*b+c that the reference physics engine
  # never used — keeps per-step error small so the tolerance stays easy to pick.
  add_compile_options(/fp:precise /W3 /EHsc)
else()
  add_compile_options(-ffp-contract=off -Wall -Wextra)
endif()

# --- glm (header-only) -------------------------------------------------------
# Replaces the previous hand-rolled Vec3; double-precision glm::dvec3/dquat match Vec3's prior
# double precision. glm::normalize()/glm::cross()/etc. use a different internal operation order
# than the old hand-rolled Vec3, so parity fixture tolerances were loosened accordingly — see
# cpp/test-data/traces/README.md.
#
# cpp/tungsten-monoxide links MassivePolyPusher's prebuilt libraries (ext/massivepolypusher), which
# vendor their own glm 0.9.6 copy and pass glm-typed values across that prebuilt ABI boundary. A
# second, differently-versioned glm (e.g. a fresh FetchContent) produces a distinct
# glm::tvec3<double, Q> type in translation units that see both copies, breaking ODR and failing to
# link against core's own exported functions. Reuse MassivePolyPusher's vendored copy when it's
# present (the combined cpp/ configure, and any build alongside tungsten-monoxide) so exactly one
# glm ABI exists in the whole binary; fall back to FetchContent only for a standalone core/editor
# configure that never links tungsten-monoxide at all.
set(TOX_VENDORED_GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher/vendor/include")
if (EXISTS "${TOX_VENDORED_GLM_DIR}/glm/glm.hpp")
  add_library(glm INTERFACE)
  add_library(glm::glm ALIAS glm)
  target_include_directories(glm INTERFACE "${TOX_VENDORED_GLM_DIR}")
else()
  include(FetchContent)
  FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glm)
  # The old hand-rolled Vec3's default constructor zero-initialized; glm 1.0's does not unless this
  # is defined (MassivePolyPusher's vendored 0.9.6 above zero-inits by default already).
  target_compile_definitions(glm INTERFACE GLM_FORCE_CTOR_INIT)
endif()

# The physics engine itself: TrackCore/Track/Ship/Zone/Trigger/Simulation,
# built once as a static library so anything embedding the engine (the parity
# replayer today, a future native game host) links `core` instead of recompiling
# its translation units.
set(CORE_HEADERS
  include/TrackCore.hpp
  include/TrackDefinition.hpp
  include/TrackGeometry.hpp
  include/TrackCollision.hpp
  include/TrackMesh.hpp
  include/Track.hpp
  include/Ship.hpp
  include/Simulation.hpp
  include/StartGrid.hpp
  include/ShipFactory.hpp
  include/GameSession.hpp)

add_library(core STATIC
  src/TrackCore.cpp
  src/Track.cpp
  src/TrackBake.cpp
  src/TrackCollision.cpp
  src/TrackMesh.cpp
  src/TrackLoader.cpp
  src/Simulation.cpp
  src/Ship.cpp
  src/StartGrid.cpp
  src/ShipFactory.cpp
  src/GameSession.cpp
  ${CORE_HEADERS})
target_include_directories(core PUBLIC include PRIVATE third_party)
target_link_libraries(core PUBLIC glm::glm PRIVATE Willpower::Geometry)

# Headers carry no build step of their own, but listing them as sources (above)
# surfaces them in IDE project views; group them under their own filter there
# rather than mixed in with the .cpp files.
source_group("Header Files" FILES ${CORE_HEADERS})

add_executable(parity tests/parity_main.cpp)
target_include_directories(parity PRIVATE third_party)
target_link_libraries(parity PRIVATE core)

# Focused loader, bake, topology, render-geometry, and mesh-query tests without
# making the existing baked-world parity replayer responsible for everything.
add_executable(track_tests tests/track_tests.cpp)
target_include_directories(track_tests PRIVATE third_party)
target_link_libraries(track_tests PRIVATE core)

# Cross-language renderer-neutral geometry parity over deterministic randomly
# generated current-schema JSON tracks.
add_executable(random_geometry_parity tests/random_geometry_parity_main.cpp)
target_include_directories(random_geometry_parity PRIVATE third_party)
target_link_libraries(random_geometry_parity PRIVATE core)

# Raw-session parity (NATIVE_GAME_RUNTIME_PLAN.md): native ship/session
# initialization and GameSession frame-stepping/events, independently from
# schema-10 JSON plus scripted controls only.
add_executable(raw_session_parity tests/raw_session_parity_main.cpp)
target_include_directories(raw_session_parity PRIVATE third_party)
target_link_libraries(raw_session_parity PRIVATE core)

# Multi-config generators leave Willpower DLLs in sibling output directories.
# Put runtime dependencies beside each executable so direct runs and CTest use
# the same self-contained layout.
if (WIN32 AND (BUILD_SHARED_LIBS OR WILLPOWER_BUILD_SHARED_LIBS))
  foreach(_executable parity track_tests random_geometry_parity raw_session_parity)
    add_custom_command(TARGET ${_executable} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:${_executable}> $<TARGET_FILE_DIR:${_executable}>
      COMMAND_EXPAND_LISTS)
  endforeach()
endif()

enable_testing()
# test-data/ (the committed physics/geometry golden corpus) lives under cpp/, a sibling of core/.
set(TRACE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../test-data/traces)
set(MESH_FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../test-data/fixtures/mesh)
set(RANDOM_GEOMETRY_FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../test-data/fixtures/random-track-mesh)
set(RAW_TRACE_DIR ${TRACE_DIR}/raw)
add_test(NAME parity
  COMMAND parity
    ${TRACE_DIR}/starter-circle.json
    ${TRACE_DIR}/open-curve.json
    ${TRACE_DIR}/boost-circuit.json
    ${TRACE_DIR}/recovery-run.json)
add_test(NAME raw_parity
  COMMAND parity
    ${RAW_TRACE_DIR}/raw-corridor-mesh-corridor.json
    ${RAW_TRACE_DIR}/raw-bare-ledge-lower-landing.json
    ${RAW_TRACE_DIR}/raw-head-on-rail.json
    ${RAW_TRACE_DIR}/raw-glancing-rail.json
    ${RAW_TRACE_DIR}/raw-corner-rail.json
    ${RAW_TRACE_DIR}/raw-airborne-outside-below-rail.json
    ${RAW_TRACE_DIR}/raw-airborne-clears-rail.json
    ${RAW_TRACE_DIR}/raw-overlapping-elevations.json
    ${RAW_TRACE_DIR}/raw-mesh-boost-checkpoint.json
    ${RAW_TRACE_DIR}/raw-hole-traversal.json
    ${RAW_TRACE_DIR}/raw-curved-banked-native-bake.json
    ${RAW_TRACE_DIR}/raw-long-mixed-course.json)
add_test(NAME track_tests COMMAND track_tests ${MESH_FIXTURE_DIR})
add_test(NAME random_geometry_parity COMMAND random_geometry_parity ${RANDOM_GEOMETRY_FIXTURE_DIR})

set(RAW_SESSION_TRACE_DIR ${TRACE_DIR}/raw-session)
add_test(NAME raw_session_init_parity
  COMMAND raw_session_parity
    ${RAW_SESSION_TRACE_DIR}/init/raw-session-init-closed-reversed.json
    ${RAW_SESSION_TRACE_DIR}/init/raw-session-init-open.json
    ${RAW_SESSION_TRACE_DIR}/init/raw-session-init-narrow.json)
add_test(NAME raw_session_step_parity
  COMMAND raw_session_parity
    ${RAW_SESSION_TRACE_DIR}/steps/raw-session-step-lap.json
    ${RAW_SESSION_TRACE_DIR}/steps/raw-session-step-roster.json
    ${RAW_SESSION_TRACE_DIR}/steps/raw-session-step-respawn.json)
