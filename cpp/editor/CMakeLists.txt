# cpp/editor — the native ImGui/SDL2/OpenGL track editor. Sits alongside cpp/core and
# cpp/willpower and links both: `core` for schema-10 loading/baking/preview (reused unmodified as a
# black box) and `Willpower::Geometry` transitively through `core` for mesh topology.
#
# Dear ImGui (docking branch) and its SDL2/OpenGL3 backends, plus the gl3w GL loader, are copied
# into include/ and src/ rather than pulled in as a submodule/FetchContent — see the
# README-VENDORED.md files under include/imgui and include/gl3w for exact provenance. SDL2 itself
# is fetched and built from source via FetchContent.
#
# Build (from an x64 MSVC Developer prompt, as part of the combined cpp/ configure):
#   cmake -S cpp -B cpp/build
#   cmake --build cpp/build --config Release
cmake_minimum_required(VERSION 3.20)
project(track_editor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (NOT WIN32)
  message(WARNING
    "cpp/editor targets Windows/MSVC only (matching cpp/willpower's existing constraint) — "
    "configuring anyway, but expect SDL2/OpenGL/gl3w wiring to need real portability work off "
    "Windows.")
endif()

if (MSVC)
  add_compile_options(/W3 /EHsc)
endif()

# --- SDL2 (FetchContent, built from source) ---------------------------------
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
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/imgui)
set(IMGUI_SOURCES
  src/imgui/imgui.cpp
  src/imgui/imgui_demo.cpp
  src/imgui/imgui_draw.cpp
  src/imgui/imgui_tables.cpp
  src/imgui/imgui_widgets.cpp
  src/imgui/imgui_impl_sdl2.cpp
  src/imgui/imgui_impl_opengl3.cpp)

# --- Vendored gl3w GL loader -------------------------------------------------
set(GL3W_SOURCES src/gl3w/gl3w.c)

# --- Editor-owned authoring model (M1) + views (M2/M6) + export/gen (M7a) + textures (M7b) +
# native file I/O (EDITOR_NATIVE_FILE_IO_PLAN.md M8) ---
set(EDITOR_SOURCES
  src/EditorTrackDefinition.cpp
  src/TopDownCanvas.cpp
  src/ElevationView.cpp
  src/USDExport.cpp
  src/MppModelExport.cpp
  src/TrackResourceDocument.cpp
  src/TrackResourceSave.cpp
  src/RandomTrack.cpp
  src/TextureCache.cpp
  src/MaterialsPanel.cpp
  src/FileDialog.cpp
  src/Clipboard.cpp
  src/TrackPropertiesPanel.cpp
  src/PropertiesPanel.cpp
  src/ZonesPanel.cpp
  src/TriggersPanel.cpp
  src/ReservationsPanel.cpp
  src/MeshPanel.cpp
  src/CurvesPanel.cpp
  src/HandlingPanel.cpp
  src/RandomRangesPanel.cpp
  src/EditorIni.cpp
  src/MaterialCatalog.cpp)

add_executable(track_editor
  main.cpp
  ${EDITOR_SOURCES}
  ${IMGUI_SOURCES}
  ${GL3W_SOURCES})

target_include_directories(track_editor PRIVATE
  include
  include/imgui
  include/gl3w
  third_party)

target_link_libraries(track_editor PRIVATE
  core
  Willpower::Common
  SDL2::SDL2
  SDL2::SDL2main)

add_executable(editor_track_resource_tests
  tests/track_resource_tests.cpp
  src/EditorTrackDefinition.cpp
  src/MppModelExport.cpp
  src/TrackResourceDocument.cpp
  src/TrackResourceSave.cpp
  src/FileDialog.cpp)
target_include_directories(editor_track_resource_tests PRIVATE include third_party)
target_link_libraries(editor_track_resource_tests PRIVATE core Willpower::Common)
add_test(NAME editor_track_resources
  COMMAND editor_track_resource_tests "${CMAKE_SOURCE_DIR}/test-data/fixtures/path/curved-banked.json")

if (WIN32)
  target_link_libraries(track_editor PRIVATE opengl32 ole32 shell32 uuid)
  target_link_libraries(editor_track_resource_tests PRIVATE ole32 shell32 uuid)
  # windows.h's min/max macros (pulled in transitively via <SDL.h> and gl3w.c) collide with
  # std::min/std::max and Willpower/nlohmann headers; suppress them everywhere in this target.
  target_compile_definitions(track_editor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# SDL2 (and, with multi-config generators, Willpower) leave shared-library
# runtime dependencies in sibling output directories; copy them beside the
# executable so a direct run/F5 works, matching cpp/app's track_runner.
add_custom_command(TARGET track_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:track_editor> $<TARGET_FILE_DIR:track_editor>
  COMMAND_EXPAND_LISTS)

# Editor config .ini, copied beside the executable so a direct run/F5 picks it up.
add_custom_command(TARGET track_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.ini" "$<TARGET_FILE_DIR:track_editor>/editor.ini")
