# cpp/ — root CMake project tying together the two independent C++ trees:
#
#   willpower/  the Willpower geometry/math libraries (Willpower.Common,
#               Willpower.Geometry, ...) — see willpower/CMakeLists.txt.
#   core/       the track-physics engine ported from js/track-physics.js,
#               parity-tested against the JS oracle — see core/CMakeLists.txt.
#
# core has no functional dependency on willpower yet, but is expected to grow
# one (mesh-region physics is planned to consume Willpower.Geometry — see
# CPP_PORT_PLAN.md). willpower is therefore built first, pinned explicitly via
# add_dependencies() below so the ordering holds under a parallel build too,
# not just by add_subdirectory() call order.
#
# Each of core/ and willpower/ can also still be configured standalone (see
# their own CMakeLists.txt header comments for those commands); this root
# project is an additional, combined entry point — use a build directory
# distinct from either of theirs to avoid a stale-cache mismatch:
#   cmake -S cpp -B cpp/build-all -G Ninja
#   cmake --build cpp/build-all
cmake_minimum_required(VERSION 3.20)
project(TungstenOxideCpp CXX)

add_subdirectory(willpower)
add_subdirectory(core)

foreach(_willpower_target Willpower.Common Willpower.Geometry)
  if (TARGET ${_willpower_target})
    add_dependencies(core ${_willpower_target})
    if (TARGET parity)
      add_dependencies(parity ${_willpower_target})
    endif()
  endif()
endforeach()
