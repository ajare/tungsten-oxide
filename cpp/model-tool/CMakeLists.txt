# cpp/model-tool — model_tool: a standalone ImGui/SDL3/mpp app that imports 3D model files (OBJ/
# FBX/USD/glTF, via AssImp) into MassivePolyPusher's runtime shape, previews them in a live mpp
# viewport, and saves them out as .mppmodel. See docs/adr/0001-model-tool.md for the design.
#
# Docked menubar/toolbar/statusbar/panel shell mirrors cpp/editor's own SDL3 + Dear ImGui (docking
# branch) bootstrap (vendored copies under include/imgui, src/imgui), including GLEW as the ImGui
# OpenGL3 backend's GL loader (ADR D2; see include/imgui/imconfig.h). This app links real
# mpp::RenderSystem for its viewport (ADR D9); cpp/editor used to vendor gl3w instead, but moved to
# GLEW too in docs/GLTF_IMPORT_PLAN.md M3, so both apps now share one loader choice.
#
# Build (from an x64 MSVC Developer prompt, as part of the combined cpp/ configure):
#   cmake -S cpp -B cpp/build
#   cmake --build cpp/build --config Release
cmake_minimum_required(VERSION 3.20)
project(model_tool C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (NOT WIN32)
  message(WARNING
    "cpp/model-tool is Windows-only; its platform guards intentionally fail compilation "
    "off Windows.")
endif()

# --- Shared glTF/.mppmodel conversion layer (docs/GLTF_IMPORT_PLAN.md, M2) ----------------------
# AssImpImport.cpp and MppSave.cpp are adapters over model_io now rather than their own AssImp walk
# and ModelSerializer driving, and ModelResources.cpp takes the 36-byte layout from there too.
if (NOT TARGET model_io_gltf)
  add_subdirectory(../model-io ${CMAKE_CURRENT_BINARY_DIR}/model-io)
endif()

if (MSVC)
  add_compile_options(/W3 /EHsc)
endif()

# --- SDL3 (MassivePolyPusher source submodule) ------------------------------
# Same SDL3 target as cpp/editor, pinned by MassivePolyPusher's nested submodule.
if (NOT TARGET SDL3::SDL3)
  set(TOX_SDL_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/willpower/ext/massive-poly-pusher/ext/sdl")
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
endif()

# --- Vendored Dear ImGui (docking) + backends -------------------------------
set(IMGUI_SOURCES
  src/imgui/imgui.cpp
  src/imgui/imgui_demo.cpp
  src/imgui/imgui_draw.cpp
  src/imgui/imgui_tables.cpp
  src/imgui/imgui_widgets.cpp
  src/imgui/imgui_impl_sdl3.cpp
  src/imgui/imgui_impl_opengl3.cpp)

set(MODEL_TOOL_SOURCES
  src/FileDialog.cpp
  src/AssImpImport.cpp
  src/NormalSmoothing.cpp
  src/ObjSmoothingGroups.cpp
  src/OpenTarget.cpp
  src/TextureLoad.cpp
  src/OrbitCamera.cpp
  src/ModelResources.cpp
  src/MaterialXmlImport.cpp
  src/MaterialLibrary.cpp
  src/MppModelImport.cpp
  src/Viewport.cpp
  src/MppSave.cpp)

add_executable(model_tool
  main.cpp
  ${MODEL_TOOL_SOURCES}
  ${IMGUI_SOURCES})
if (COMMAND tox_copy_asan_runtime)
  tox_copy_asan_runtime(model_tool)
endif()

target_compile_features(model_tool PRIVATE cxx_std_20)

# --- MassivePolyPusher (mpp/mpp-helper/mpp-mesh/ext-utils) + its vendored deps -------------------
# mpp-program (the shader-annotation-DSL parser, mpp::program::Parser) is deliberately not linked:
# it's an internal dependency of MassivePolyPusher.dll itself (used by RenderSystem::
# createCoreResources() to build the default 3D program model-tool's materials resolve to, see
# ModelResources.hpp), not something this app calls directly -- matching
# cpp/tungsten-monoxide/CMakeLists.txt's own link set, which also omits it.
set(_model_tool_legacy_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher")
set(_model_tool_repository_mpp_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massive-poly-pusher")
set(_model_tool_cpp_willpower_mpp_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/ext/massive-poly-pusher")
set(_model_tool_mpp_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/willpower/ext/massive-poly-pusher")
if (MODEL_TOOL_MPP_DIR STREQUAL _model_tool_legacy_mpp_dir OR
    MODEL_TOOL_MPP_DIR STREQUAL _model_tool_repository_mpp_dir OR
    MODEL_TOOL_MPP_DIR STREQUAL _model_tool_cpp_willpower_mpp_dir)
  set(MODEL_TOOL_MPP_DIR "${_model_tool_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency" FORCE)
else()
  set(MODEL_TOOL_MPP_DIR "${_model_tool_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency")
endif()
unset(_model_tool_legacy_mpp_dir)
unset(_model_tool_repository_mpp_dir)
unset(_model_tool_cpp_willpower_mpp_dir)
unset(_model_tool_mpp_dir)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/MppBuildTree.cmake")
tox_resolve_mpp_build_tree("${MODEL_TOOL_MPP_DIR}")
target_include_directories(model_tool PRIVATE
  include
  include/imgui
  include/stb
  "${MODEL_TOOL_MPP_DIR}/mpp/include"
  "${MODEL_TOOL_MPP_DIR}/mpp-helper/include"
  "${MODEL_TOOL_MPP_DIR}/mpp-mesh/include"
  # mpp-program isn't linked (see the comment above), but mpp/UniformCollection.h -- a *public* mpp
  # header pulled in transitively -- still #includes mpp/program/glslTypes.h, so its include dir is
  # still needed even though model-tool never calls into mpp::program:: itself.
  "${MODEL_TOOL_MPP_DIR}/mpp-program/include"
  "${MODEL_TOOL_MPP_DIR}/vendor/include"
  "${TOX_MPP_GLEW_INCLUDE_DIR}"
  # No Assimp include directory: since M2 this app calls no AssImp API directly (the walk lives in
  # model_io_gltf, which carries its own PUBLIC Assimp includes for anything that still needs them).
  "${MODEL_TOOL_MPP_DIR}/ext/utils/include")

target_link_directories(model_tool PRIVATE
  "${TOX_MPP_BUILD_DIR}/lib/$<CONFIG>")

target_link_libraries(model_tool PRIVATE
  SDL3::SDL3
  # Carries AssImp and the mpp include/link set transitively. The explicit mpp entries below are
  # kept because this app still calls mpp directly for its viewport; AssImp's are not, because it
  # no longer calls AssImp at all.
  model_io_gltf
  # Willpower.Common's XmlReader parses imported Resources.xml-shaped material files (see
  # MaterialXmlImport.hpp) -- a real CMake IMPORTED-adjacent target (unlike the raw-lib-name mpp
  # libraries below), so $<TARGET_RUNTIME_DLLS:model_tool> picks up its DLL automatically.
  Willpower.Common
  # <Model> XML fragment schema (docs/adr/0003-model-xml-layer.md), also TinyXML2-via-Willpower.Common
  # under the hood -- shared with cpp/editor, see cpp/model-xml/CMakeLists.txt.
  model_xml
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  $<IF:$<CONFIG:Debug>,MppHelperd,MppHelper>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>)

if (WIN32)
  # The dynamic GLEW import library: both model_tool and MassivePolyPusher.dll must share GLEW's one
  # process-wide function-pointer table. Assimp is no longer linked here -- it arrives transitively
  # through model_io_gltf, which is the only thing that calls it now.
  target_link_libraries(model_tool PRIVATE
    $<IF:$<CONFIG:Debug>,glew32d,glew32>
    opengl32
    ole32
    shell32
    uuid)
  # windows.h's min/max macros (pulled in transitively via <SDL3/SDL.h>/GLEW/mpp headers) collide with
  # std::min/std::max; suppress them everywhere in this target (same fix cpp/editor applies).
  target_compile_definitions(model_tool PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

# --- Runtime deployment: SDL3's own DLL, plus MassivePolyPusher/AssImp DLLs, none of which CMake's
# $<TARGET_RUNTIME_DLLS:...> can discover on its own (they're linked via raw lib names + explicit
# target_link_directories, not as CMake IMPORTED targets) -- mirrors cpp/launcher's own generated-
# script approach for the identical problem, just scoped to what model_tool actually links. ---------
set(_model_tool_copy_runtime_script [=[
if (NOT DEFINED OUTPUT_DIR OR NOT DEFINED CONFIG)
  message(FATAL_ERROR "OUTPUT_DIR and CONFIG are required")
endif()

# The dependency's CMake build places Assimp, MassivePolyPusher, MppMesh, MppProgram, MppHelper,
# Utils, and GLEW runtime DLLs together.
set(_runtime_root "@TOX_MPP_BUILD_DIR@/bin/${CONFIG}")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
if (EXISTS "${_runtime_root}")
  file(GLOB _runtime_files LIST_DIRECTORIES false "${_runtime_root}/*")
  if (_runtime_files)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_runtime_files} "${OUTPUT_DIR}"
      COMMAND_ERROR_IS_FATAL ANY)
  endif()
endif()
]=])
file(CONFIGURE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/CopyModelToolRuntime.cmake"
  CONTENT "${_model_tool_copy_runtime_script}"
  @ONLY)
unset(_model_tool_copy_runtime_script)

add_custom_command(TARGET model_tool POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:model_tool> $<TARGET_FILE_DIR:model_tool>
  COMMAND_EXPAND_LISTS)
if (WIN32)
  add_custom_command(TARGET model_tool POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DOUTPUT_DIR=$<TARGET_FILE_DIR:model_tool>"
      "-DCONFIG=$<CONFIG>"
      -P "${CMAKE_CURRENT_BINARY_DIR}/CopyModelToolRuntime.cmake"
    COMMAND_EXPAND_LISTS)
endif()

# --- Headless tests (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.1/4.2) ---------------------------
# NormalSmoothing.cpp/CollidableFlag.cpp deliberately depend on nothing but AssImpImport.hpp's
# plain data types (no AssImp, no mpp, no SDL/GPU) specifically so they're testable this cheaply --
# unlike the rest of model_tool, this target needs no runtime-DLL deployment at all.
enable_testing()
add_executable(model_tool_tests
  tests/model_tool_tests.cpp
  src/AssImpImport.cpp
  src/NormalSmoothing.cpp
  src/ObjSmoothingGroups.cpp
  src/OpenTarget.cpp)
if (COMMAND tox_copy_asan_runtime)
  tox_copy_asan_runtime(model_tool_tests)
endif()
target_include_directories(model_tool_tests PRIVATE include)
target_compile_features(model_tool_tests PRIVATE cxx_std_20)
# AssImpImport.cpp is now an adapter over model_io_gltf, so this target links it and gains the
# mpp/AssImp runtime DLLs. That gives up the "needs neither AssImp nor a GPU" property this target
# used to have, in exchange for actually covering importModel() -- the one part of model_tool M2
# changed behaviourally, and otherwise only reachable through the GUI.
target_link_libraries(model_tool_tests PRIVATE model_xml model_io_gltf)
target_compile_definitions(model_tool_tests PRIVATE
  "MODEL_TOOL_GLTF_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../test-data/fixtures/gltf\"")
add_test(NAME model_tool_tests COMMAND model_tool_tests)
model_io_deploy_runtime(model_tool_tests)

# Willpower.Common is a shared library on multi-config generators; copy it beside the test exe so
# `ctest` can actually launch it (OpenTarget.cpp/model_xml both use TinyXML2 now).
add_custom_command(TARGET model_tool_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:model_tool_tests> $<TARGET_FILE_DIR:model_tool_tests>
  COMMAND_EXPAND_LISTS)
