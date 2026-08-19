# cpp/ — root CMake project tying together the independent C++ trees:
#
#   ../ext/willpower/  the Willpower geometry/math libraries (Willpower.Common,
#               Willpower.Geometry, ...) — see ../ext/willpower/CMakeLists.txt.
#   ../ext/willpower/willpower.application/  Willpower's application/resource-system
#               library — see ../ext/willpower/willpower.application/CMakeLists.txt.
#   core/       the track-physics engine, checked against a committed golden fixture/trace
#               corpus — see core/CMakeLists.txt.
#   app/        track_runner, a headless CLI session host — see app/CMakeLists.txt.
#   launcher/   Windows host for a Willpower application DLL — see
#               launcher/CMakeLists.txt.
#   applib/     shared gameplay/entity/state primitives — see
#               applib/CMakeLists.txt.
#   tungsten-monoxide/  TungstenMonoxide application DLL — see
#               tungsten-monoxide/CMakeLists.txt.
#   editor/     track_editor, the native ImGui/SDL3/OpenGL track editor — see
#               editor/CMakeLists.txt and ../EDITOR_CPP_PORT_PLAN.md.
#   model-tool/ model_tool, the AssImp import/preview/save tool for MassivePolyPusher's model
#               format — see model-tool/CMakeLists.txt and ../docs/adr/0001-model-tool.md.
#   model-xml/  the shared <Model> XML fragment schema (id/ModelFile/TrackData/per-mesh Type+
#               Visible), linked by both editor/ and model-tool/ — see model-xml/CMakeLists.txt,
#               ../docs/adr/0003-model-xml-layer.md, and ../docs/TRACK_MODEL_LIST_PLAN.md.
#   model-io/   the shared asset/.mppmodel conversion layer: model_io_core (vertex-layout contract,
#               tangent derivation, MeshSpecification-driven packing, embedded PbrMaterial
#               construction — mpp but no AssImp) and model_io_gltf (AssImp import + validation).
#               See model-io/CMakeLists.txt, ../docs/adr/0004-gltf-import.md and
#               ../docs/GLTF_IMPORT_PLAN.md.
#   gltf-convert/ gltf_convert, the headless glTF -> .mppmodel converter — see
#               gltf-convert/CMakeLists.txt.
#
# Willpower and MassivePolyPusher are built separately under ../ext/willpower/build. This project
# imports their Debug, Release, and MemCheck binaries and builds the supplemental MPP targets that Willpower does not consume
# before their first cpp consumer. core/ can also be configured standalone and uses the same
# imports; see README.md for the required dependency build commands.
# Use a cpp build directory distinct from the dependency build tree:
#   cmake -S cpp -B cpp/build-all -G Ninja
#   cmake --build cpp/build-all
cmake_minimum_required(VERSION 3.20)
project(TungstenOxideCpp C CXX)

# MemCheck is a Debug variant instrumented with MSVC AddressSanitizer. Keep its
# base flags and runtime library aligned with Debug, except /RTC1 and incremental
# linking, which MSVC ASan does not support. Dependencies imported below provide
# matching MemCheck binaries from the standalone Willpower build tree.
if (MSVC AND CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CONFIGURATION_TYPES "Debug;Release;MemCheck" CACHE STRING
    "Semicolon separated list of supported configuration types" FORCE)

  string(REPLACE "/RTC1" "" TOX_MEMCHECK_CXX_FLAGS "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "/RTC1" "" TOX_MEMCHECK_C_FLAGS "${CMAKE_C_FLAGS_DEBUG}")
  set(CMAKE_CXX_FLAGS_MEMCHECK "${TOX_MEMCHECK_CXX_FLAGS} /fsanitize=address" CACHE STRING
    "Flags used by the C++ compiler for MemCheck builds" FORCE)
  set(CMAKE_C_FLAGS_MEMCHECK "${TOX_MEMCHECK_C_FLAGS} /fsanitize=address" CACHE STRING
    "Flags used by the C compiler for MemCheck builds" FORCE)

  string(REPLACE "/INCREMENTAL" "" TOX_MEMCHECK_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
  string(REPLACE "/INCREMENTAL" "" TOX_MEMCHECK_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}")
  string(REPLACE "/INCREMENTAL" "" TOX_MEMCHECK_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS_DEBUG}")
  set(CMAKE_EXE_LINKER_FLAGS_MEMCHECK "${TOX_MEMCHECK_EXE_LINKER_FLAGS} /INCREMENTAL:NO" CACHE STRING
    "Flags used to link an executable for MemCheck builds" FORCE)
  set(CMAKE_SHARED_LINKER_FLAGS_MEMCHECK "${TOX_MEMCHECK_SHARED_LINKER_FLAGS} /INCREMENTAL:NO" CACHE STRING
    "Flags used by the shared library linker for MemCheck builds" FORCE)
  set(CMAKE_MODULE_LINKER_FLAGS_MEMCHECK "${TOX_MEMCHECK_MODULE_LINKER_FLAGS} /INCREMENTAL:NO" CACHE STRING
    "Flags used by the module linker for MemCheck builds" FORCE)
  unset(TOX_MEMCHECK_CXX_FLAGS)
  unset(TOX_MEMCHECK_C_FLAGS)
  unset(TOX_MEMCHECK_EXE_LINKER_FLAGS)
  unset(TOX_MEMCHECK_SHARED_LINKER_FLAGS)
  unset(TOX_MEMCHECK_MODULE_LINKER_FLAGS)
  mark_as_advanced(
    CMAKE_CXX_FLAGS_MEMCHECK
    CMAKE_C_FLAGS_MEMCHECK
    CMAKE_EXE_LINKER_FLAGS_MEMCHECK
    CMAKE_SHARED_LINKER_FLAGS_MEMCHECK
    CMAKE_MODULE_LINKER_FLAGS_MEMCHECK)

  set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<OR:$<CONFIG:Debug>,$<CONFIG:MemCheck>>:Debug>DLL")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<$<CONFIG:Debug,MemCheck>:ProgramDatabase>")

  # The MSVC ASan runtime is not normally on PATH outside a developer prompt.
  # Deploy it beside every MemCheck executable below, as Willpower does.
  get_filename_component(TOX_ASAN_RUNTIME_DIR "${CMAKE_LINKER}" DIRECTORY)
  set(TOX_ASAN_DLL "${TOX_ASAN_RUNTIME_DIR}/clang_rt.asan_dynamic-x86_64.dll")
  set(TOX_ASAN_PDB "${TOX_ASAN_RUNTIME_DIR}/clang_rt.asan_dynamic-x86_64.pdb")
  if (NOT EXISTS "${TOX_ASAN_DLL}")
    message(WARNING "Could not find the MSVC ASan runtime DLL at ${TOX_ASAN_DLL}; MemCheck executables may fail to start.")
  endif()
endif()

function(tox_copy_asan_runtime target)
  if (NOT DEFINED TOX_ASAN_DLL)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "$<$<CONFIG:MemCheck>:${CMAKE_COMMAND};-E;copy_if_different;${TOX_ASAN_DLL};${TOX_ASAN_PDB};$<TARGET_FILE_DIR:${target}>>"
    COMMAND_EXPAND_LISTS
    VERBATIM)
endfunction()

# Build SDL3 once from MassivePolyPusher's pinned source submodule. All cpp/
# applications consume this target instead of carrying or fetching their own SDL copies.
set(TOX_SDL_SOURCE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../ext/willpower/ext/massive-poly-pusher/ext/sdl")
if (NOT EXISTS "${TOX_SDL_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "SDL3 submodule is missing; run: git submodule update --init --recursive")
endif()
set(SDL_SHARED ON CACHE BOOL "Build SDL3 as a shared library" FORCE)
set(SDL_STATIC OFF CACHE BOOL "Do not build the SDL3 static library" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build the SDL3 test library" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Do not build SDL3 tests" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Do not build SDL3 examples" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "Do not install SDL3" FORCE)
add_subdirectory("${TOX_SDL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sdl3" EXCLUDE_FROM_ALL)

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

# Make Native File Dialog Extended available from source in place of the old
# prebuilt nfd libraries. Current applications retain their existing dialog
# wrappers; the source-built target is available for consumers as nfd::nfd.
set(NFD_BUILD_TESTS OFF CACHE BOOL "Do not build Native File Dialog tests" FORCE)
set(NFD_BUILD_SDL2_TESTS OFF CACHE BOOL "Do not build Native File Dialog SDL2 tests" FORCE)
set(NFD_BUILD_GLFW3_TESTS OFF CACHE BOOL "Do not build Native File Dialog GLFW tests" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "Do not install Native File Dialog" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ext/nativefiledialog-extended"
                 "${CMAKE_CURRENT_BINARY_DIR}/nativefiledialog-extended")

# Build GoogleTest from its repository submodule instead of carrying prebuilt
# configuration-specific libraries under third_party.
set(BUILD_GMOCK OFF CACHE BOOL "Do not build GoogleMock" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "Do not install GoogleTest" FORCE)
set(gtest_build_tests OFF CACHE BOOL "Do not build GoogleTest's own tests" FORCE)
set(gtest_build_samples OFF CACHE BOOL "Do not build GoogleTest samples" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime for GoogleTest" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ext/googletest"
                 "${CMAKE_CURRENT_BINARY_DIR}/googletest")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WillpowerBuildTree.cmake")
tox_import_willpower("${CMAKE_CURRENT_SOURCE_DIR}/../ext/willpower")

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(applib)
add_subdirectory(launcher)
add_subdirectory(tungsten-monoxide)
add_subdirectory(model-xml)
add_subdirectory(model-io)
add_subdirectory(editor)
add_subdirectory(model-tool)
add_subdirectory(gltf-convert)

# The launcher dynamically loads the application DLL rather than linking it,
# so express the build/deployment relationship explicitly. TungstenMonoxide's
# post-build step copies itself and its runtime DLLs beside Launcher.
if (TARGET Launcher AND TARGET TungstenMonoxide)
  add_dependencies(Launcher TungstenMonoxide)
endif()
