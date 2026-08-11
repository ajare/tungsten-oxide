# cpp/model-tool — model_tool: a standalone ImGui/SDL2/mpp app that imports 3D model files (OBJ/
# FBX/USD/glTF, via AssImp) into MassivePolyPusher's runtime shape, previews them in a live mpp
# viewport, and saves them out as .mppmodel. See docs/adr/0001-model-tool.md for the design.
#
# Docked menubar/toolbar/statusbar/panel shell mirrors cpp/editor's own SDL2 + Dear ImGui (docking
# branch) bootstrap (vendored copies under include/imgui, src/imgui) -- but unlike editor, this app
# links real mpp::RenderSystem for its viewport (ADR D9), so its ImGui OpenGL3 backend shares GLEW
# as its GL loader instead of editor's vendored gl3w (ADR D2; see include/imgui/imconfig.h).
#
# Build (from an x64 MSVC Developer prompt, as part of the combined cpp/ configure):
#   cmake -S cpp -B cpp/build
#   cmake --build cpp/build --config Release
cmake_minimum_required(VERSION 3.20)
project(model_tool CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (NOT WIN32)
  message(WARNING
    "cpp/model-tool targets Windows/MSVC only (matching cpp/editor's and cpp/tungsten-monoxide's "
    "existing constraint) -- configuring anyway, but expect SDL2/OpenGL/mpp wiring to need real "
    "portability work off Windows.")
endif()

if (MSVC)
  add_compile_options(/W3 /EHsc)
endif()

# --- SDL2 (FetchContent, built from source) ---------------------------------
# Same SDL2, same version, as cpp/editor -- model-tool copies editor's exact docked-shell
# bootstrap (ADR 0001 "windowing lib" decision), so it uses the same backend (imgui_impl_sdl2).
include(FetchContent)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  SDL2
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-2.30.9
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL2)

# --- Vendored Dear ImGui (docking) + backends -------------------------------
set(IMGUI_SOURCES
  src/imgui/imgui.cpp
  src/imgui/imgui_demo.cpp
  src/imgui/imgui_draw.cpp
  src/imgui/imgui_tables.cpp
  src/imgui/imgui_widgets.cpp
  src/imgui/imgui_impl_sdl2.cpp
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

target_compile_features(model_tool PRIVATE cxx_std_20)

# --- MassivePolyPusher (mpp/mpp-helper/mpp-mesh/ext-utils) + its vendored deps -------------------
# mpp-program (the shader-annotation-DSL parser, mpp::program::Parser) is deliberately not linked:
# it's an internal dependency of MassivePolyPusher.dll itself (used by RenderSystem::
# createCoreResources() to build the default 3D program model-tool's materials resolve to, see
# ModelResources.hpp), not something this app calls directly -- matching
# cpp/tungsten-monoxide/CMakeLists.txt's own link set, which also omits it.
set(_model_tool_legacy_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher")
set(_model_tool_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massive-poly-pusher")
if (MODEL_TOOL_MPP_DIR STREQUAL _model_tool_legacy_mpp_dir)
  set(MODEL_TOOL_MPP_DIR "${_model_tool_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency" FORCE)
else()
  set(MODEL_TOOL_MPP_DIR "${_model_tool_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency")
endif()
unset(_model_tool_legacy_mpp_dir)
unset(_model_tool_mpp_dir)
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
  "${MODEL_TOOL_MPP_DIR}/build/cmake/_deps/glew-2.3.1/include"
  # Use the Assimp headers and generated configuration matching the binaries built by MPP.
  "${MODEL_TOOL_MPP_DIR}/build/cmake/ext/assimp/include"
  "${MODEL_TOOL_MPP_DIR}/ext/assimp/include"
  "${MODEL_TOOL_MPP_DIR}/ext/utils/include")

target_link_directories(model_tool PRIVATE
  "${MODEL_TOOL_MPP_DIR}/build/cmake/lib/$<CONFIG>")

target_link_libraries(model_tool PRIVATE
  SDL2::SDL2
  SDL2::SDL2main
  # Willpower::Common's XmlReader parses imported Resources.xml-shaped material files (see
  # MaterialXmlImport.hpp) -- a real CMake IMPORTED-adjacent target (unlike the raw-lib-name mpp
  # libraries below), so $<TARGET_RUNTIME_DLLS:model_tool> picks up its DLL automatically.
  Willpower::Common
  # <Model> XML fragment schema (docs/adr/0003-model-xml-layer.md), also TinyXML2-via-Willpower::Common
  # under the hood -- shared with cpp/editor, see cpp/model-xml/CMakeLists.txt.
  model_xml
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  $<IF:$<CONFIG:Debug>,MppHelperd,MppHelper>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>
  $<IF:$<CONFIG:Debug>,assimp-vc145-mtd,assimp-vc145-mt>
  # The dynamic import lib (glew32), not the static glew32s -- must be the exact same GLEW as
  # MassivePolyPusher.dll itself links (also dynamic), so both it and model_tool's own GL calls
  # (imgui_impl_opengl3.cpp, main.cpp) share the one process-wide GLEW function-pointer table that
  # RenderSystem's constructor initializes via glewInit(). Linking the static lib instead would give
  # model_tool its own separate, never-glewInit'd pointer table.
  glew32)

if (WIN32)
  target_link_libraries(model_tool PRIVATE opengl32 ole32 shell32 uuid)
  # windows.h's min/max macros (pulled in transitively via <SDL.h>/GLEW/mpp headers) collide with
  # std::min/std::max; suppress them everywhere in this target (same fix cpp/editor applies).
  target_compile_definitions(model_tool PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

# --- Runtime deployment: SDL2's own DLL, plus MassivePolyPusher/AssImp DLLs, none of which CMake's
# $<TARGET_RUNTIME_DLLS:...> can discover on its own (they're linked via raw lib names + explicit
# target_link_directories, not as CMake IMPORTED targets) -- mirrors cpp/launcher's own generated-
# script approach for the identical problem, just scoped to what model_tool actually links. ---------
set(_model_tool_copy_runtime_script [=[
if (NOT DEFINED OUTPUT_DIR OR NOT DEFINED CONFIG)
  message(FATAL_ERROR "OUTPUT_DIR and CONFIG are required")
endif()

# The dependency's CMake build places Assimp, MassivePolyPusher, MppMesh, MppProgram, MppHelper,
# Utils, and GLEW runtime DLLs together.
set(_runtime_root "@MODEL_TOOL_MPP_DIR@/build/cmake/bin/${CONFIG}")

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
      "-DCONFIG=$<IF:$<CONFIG:Debug>,Debug,Release>"
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
  src/NormalSmoothing.cpp
  src/ObjSmoothingGroups.cpp
  src/OpenTarget.cpp)
target_include_directories(model_tool_tests PRIVATE include)
target_compile_features(model_tool_tests PRIVATE cxx_std_20)
target_link_libraries(model_tool_tests PRIVATE model_xml)
add_test(NAME model_tool_tests COMMAND model_tool_tests)

# Willpower.Common is a shared library on multi-config generators; copy it beside the test exe so
# `ctest` can actually launch it (OpenTarget.cpp/model_xml both use TinyXML2 now).
add_custom_command(TARGET model_tool_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:model_tool_tests> $<TARGET_FILE_DIR:model_tool_tests>
  COMMAND_EXPAND_LISTS)
