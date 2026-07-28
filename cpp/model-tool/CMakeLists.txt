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
  src/TextureLoad.cpp
  src/OrbitCamera.cpp
  src/ModelResources.cpp
  src/ModelResourceExport.cpp
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
set(MODEL_TOOL_MPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher" CACHE PATH
  "Root of the MassivePolyPusher dependency")
set(MODEL_TOOL_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party" CACHE PATH
  "Root of model_tool's third-party SDK headers and libraries (AssImp)")
set(MODEL_TOOL_LIB_ARCH x64 CACHE STRING "Architecture used by the MassivePolyPusher/AssImp SDK libraries")

target_include_directories(model_tool PRIVATE
  include
  include/imgui
  include/stb
  # Listed before vendor/include on purpose: both it and vendor/include/ have their own assimp/
  # headers (vendor's is an older bundled copy lacking aiTextureType_BASE_COLOR/
  # aiScene::GetEmbeddedTexture, which AssImpImport.cpp needs) -- first match on the include path
  # wins, and this app was asked to use the third_party/ AssImp specifically.
  "${MODEL_TOOL_THIRD_PARTY_DIR}/include"
  "${MODEL_TOOL_MPP_DIR}/mpp/include"
  "${MODEL_TOOL_MPP_DIR}/mpp-helper/include"
  "${MODEL_TOOL_MPP_DIR}/mpp-mesh/include"
  # mpp-program isn't linked (see the comment above), but mpp/UniformCollection.h -- a *public* mpp
  # header pulled in transitively -- still #includes mpp/program/glslTypes.h, so its include dir is
  # still needed even though model-tool never calls into mpp::program:: itself.
  "${MODEL_TOOL_MPP_DIR}/mpp-program/include"
  "${MODEL_TOOL_MPP_DIR}/vendor/include"
  "${MODEL_TOOL_MPP_DIR}/ext/utils/include")

target_link_directories(model_tool PRIVATE
  "${MODEL_TOOL_MPP_DIR}/mpp/build/vs2026/lib/${MODEL_TOOL_LIB_ARCH}/$<CONFIG>"
  "${MODEL_TOOL_MPP_DIR}/mpp-helper/build/vs2026/lib/${MODEL_TOOL_LIB_ARCH}/$<CONFIG>"
  "${MODEL_TOOL_MPP_DIR}/mpp-mesh/build/vs2026/lib/${MODEL_TOOL_LIB_ARCH}/$<CONFIG>"
  "${MODEL_TOOL_MPP_DIR}/ext/utils/build/vs2026/lib/${MODEL_TOOL_LIB_ARCH}/$<CONFIG>"
  "${MODEL_TOOL_MPP_DIR}/vendor/lib/vs2026/${MODEL_TOOL_LIB_ARCH}/$<CONFIG>"
  "${MODEL_TOOL_THIRD_PARTY_DIR}/lib/vs2026/${MODEL_TOOL_LIB_ARCH}/$<CONFIG>")

target_link_libraries(model_tool PRIVATE
  SDL2::SDL2
  SDL2::SDL2main
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

set(_runtime_roots
  "@MODEL_TOOL_THIRD_PARTY_DIR@/bin/vs2026/@MODEL_TOOL_LIB_ARCH@/${CONFIG}"
  "@MODEL_TOOL_MPP_DIR@/vendor/bin/vs2026/@MODEL_TOOL_LIB_ARCH@/${CONFIG}"
  "@MODEL_TOOL_MPP_DIR@/mpp/build/vs2026/bin/@MODEL_TOOL_LIB_ARCH@/${CONFIG}"
  "@MODEL_TOOL_MPP_DIR@/mpp-mesh/build/vs2026/bin/@MODEL_TOOL_LIB_ARCH@/${CONFIG}"
  # MppProgram.dll: MassivePolyPusher.dll's own import table depends on it (mpp::program::Parser
  # is an internal implementation detail of RenderSystem::createCoreResources(), see
  # ModelResources.hpp) -- not something model_tool links against directly, but the OS loader still
  # needs the file present on disk at process startup.
  "@MODEL_TOOL_MPP_DIR@/mpp-program/build/vs2026/bin/@MODEL_TOOL_LIB_ARCH@/${CONFIG}"
  "@MODEL_TOOL_MPP_DIR@/mpp-helper/build/vs2026/bin/@MODEL_TOOL_LIB_ARCH@/${CONFIG}"
  "@MODEL_TOOL_MPP_DIR@/ext/utils/build/vs2026/bin/@MODEL_TOOL_LIB_ARCH@/${CONFIG}")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(_runtime_root IN LISTS _runtime_roots)
  if (EXISTS "${_runtime_root}")
    file(GLOB _runtime_files LIST_DIRECTORIES false "${_runtime_root}/*")
    if (_runtime_files)
      execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_runtime_files} "${OUTPUT_DIR}"
        COMMAND_ERROR_IS_FATAL ANY)
    endif()
  endif()
endforeach()
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
