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
# only creates imported targets for their prebuilt binaries. core/ can also be configured
# standalone and uses the same imports; see README.md for the required dependency build commands.
# Use a cpp build directory distinct from the dependency build tree:
#   cmake -S cpp -B cpp/build-all -G Ninja
#   cmake --build cpp/build-all
cmake_minimum_required(VERSION 3.20)
project(TungstenOxideCpp C CXX)

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
