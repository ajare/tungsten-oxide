# cpp/ — root CMake project tying together the independent C++ trees:
#
#   willpower/  the Willpower geometry/math libraries (Willpower.Common,
#               Willpower.Geometry, ...) — see willpower/CMakeLists.txt.
#   willpower/willpower.application/  Willpower's application/resource-system
#               library — see willpower/willpower.application/CMakeLists.txt.
#   core/       the track-physics engine, checked against a committed golden fixture/trace
#               corpus — see core/CMakeLists.txt.
#   app/        track_runner, a headless CLI session host — see app/CMakeLists.txt.
#   launcher/   Windows host for a Willpower application DLL — see
#               launcher/CMakeLists.txt.
#   applib/     shared gameplay/entity/state primitives — see
#               applib/CMakeLists.txt.
#   tungsten-monoxide/  TungstenMonoxide application DLL — see
#               tungsten-monoxide/CMakeLists.txt.
#   editor/     track_editor, the native ImGui/SDL2/OpenGL track editor — see
#               editor/CMakeLists.txt and ../EDITOR_CPP_PORT_PLAN.md.
#   model-tool/ model_tool, the AssImp import/preview/save tool for MassivePolyPusher's model
#               format — see model-tool/CMakeLists.txt and ../docs/adr/0001-model-tool.md.
#   model-xml/  the shared <Model> XML fragment schema (id/ModelFile/TrackData/per-mesh Type+
#               Visible), linked by both editor/ and model-tool/ — see model-xml/CMakeLists.txt,
#               ../docs/adr/0003-model-xml-layer.md, and ../docs/TRACK_MODEL_LIST_PLAN.md.
#
# core links Willpower.Geometry for current-schema mesh topology validation and
# triangulation. The target link establishes build order; explicit dependencies
# below retain the intended ordering for all generators and executable targets.
#
# Each of core/ and willpower/ can also still be configured standalone (see
# their own CMakeLists.txt header comments for those commands); this root
# project is an additional, combined entry point — use a build directory
# distinct from either of theirs to avoid a stale-cache mismatch:
#   cmake -S cpp -B cpp/build-all -G Ninja
#   cmake --build cpp/build-all
cmake_minimum_required(VERSION 3.20)
project(TungstenOxideCpp CXX)

# Register tests from child projects in the combined build's top-level
# CTestTestfile.cmake, so the documented `ctest --test-dir cpp/build` command
# discovers core's parity and focused track tests.
enable_testing()

# Build yaml-cpp from the repository submodule instead of carrying a duplicate
# header set and prebuilt libraries under third_party.
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Do not build yaml-cpp contrib sources" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Do not build yaml-cpp tools" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Do not build yaml-cpp tests" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "Do not install yaml-cpp" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build yaml-cpp as a static library" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ext/yaml-cpp" "${CMAKE_CURRENT_BINARY_DIR}/yaml-cpp" EXCLUDE_FROM_ALL)

add_subdirectory(willpower)
add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(applib)
add_subdirectory(launcher)
add_subdirectory(tungsten-monoxide)
add_subdirectory(model-xml)
add_subdirectory(editor)
add_subdirectory(model-tool)

# The launcher dynamically loads the application DLL rather than linking it,
# so express the build/deployment relationship explicitly. TungstenMonoxide's
# post-build step copies itself and its runtime DLLs beside Launcher.
if (TARGET Launcher AND TARGET TungstenMonoxide)
  add_dependencies(Launcher TungstenMonoxide)
endif()

foreach(_willpower_target Willpower.Common Willpower.Application Willpower.Geometry)
  if (TARGET ${_willpower_target})
    add_dependencies(core ${_willpower_target})
    if (TARGET parity)
      add_dependencies(parity ${_willpower_target})
    endif()
    if (TARGET track_runner)
      add_dependencies(track_runner ${_willpower_target})
    endif()
    if (TARGET track_editor)
      add_dependencies(track_editor ${_willpower_target})
    endif()
  endif()
endforeach()
