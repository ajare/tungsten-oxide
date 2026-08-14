# cpp/editor — the native ImGui/SDL3/OpenGL track editor. Sits alongside cpp/core and
# cpp/willpower and links both: `core` for schema-10/12 loading/baking/preview (reused unmodified
# as a black box) and `Willpower::Common` directly for material catalog parsing.
#
# Dear ImGui (docking branch) and its SDL3/OpenGL3 backends are copied into include/ and src/ —
# see the README-VENDORED.md file under include/imgui for exact provenance. SDL3 is built from
# MassivePolyPusher's pinned source submodule, and the GL loader is GLEW from that same build tree
# (it was a vendored gl3w copy until docs/GLTF_IMPORT_PLAN.md M3 — see include/imgui/imconfig.h).
#
# Build (from an x64 MSVC Developer prompt, as part of the combined cpp/ configure):
#   cmake -S cpp -B cpp/build
#   cmake --build cpp/build --config Release
cmake_minimum_required(VERSION 3.20)
project(track_editor C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TOX_SHARED_THIRD_PARTY_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/include")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (NOT WIN32)
  message(WARNING
    "cpp/editor is Windows-only; its platform guards intentionally fail compilation off Windows.")
endif()

if (MSVC)
  add_compile_options(/W3 /EHsc)
endif()

# --- SDL3 (MassivePolyPusher source submodule) ------------------------------
if (NOT TARGET SDL3::SDL3)
  set(TOX_SDL_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massive-poly-pusher/ext/sdl")
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
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/imgui)
set(IMGUI_SOURCES
  src/imgui/imgui.cpp
  src/imgui/imgui_demo.cpp
  src/imgui/imgui_draw.cpp
  src/imgui/imgui_tables.cpp
  src/imgui/imgui_widgets.cpp
  src/imgui/imgui_impl_sdl3.cpp
  src/imgui/imgui_impl_opengl3.cpp)

# --- GL loader: GLEW, from MassivePolyPusher's build tree ----------------------------------------
# Was a vendored gl3w copy until docs/GLTF_IMPORT_PLAN.md M3. The editor now links mpp, whose
# headers pull in GLEW unconditionally, so a second independent loader in the same binary would be
# a duplicate-symbol hazard -- see include/imgui/imconfig.h and docs/adr/0004-gltf-import.md D1.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/MppBuildTree.cmake")
tox_resolve_mpp_build_tree("${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massive-poly-pusher")

# --- Shared .mppmodel/vertex-layout layer (docs/GLTF_IMPORT_PLAN.md, M3) + glTF import (M5) ------
# model_io_gltf (which links model_io_core PUBLIC): M3 only needed model_io_core, since the editor
# read/wrote .mppmodel files but imported no assets. M5's "Import glTF..." changes that, so
# track_editor now pulls in AssImp too -- the mpp_model_import_tests/editor_track_resource_tests
# targets below stay on model_io_core alone, since neither imports glTF.
if (NOT TARGET model_io_core)
  add_subdirectory(../model-io ${CMAKE_CURRENT_BINARY_DIR}/model-io)
endif()

# --- Editor-owned authoring model (M1) + views (M2/M6) + export/gen (M7a) + textures (M7b) +
# native file I/O (EDITOR_NATIVE_FILE_IO_PLAN.md M8) ---
set(EDITOR_SOURCES
  src/EditorTrackDefinition.cpp
  src/TopDownCanvas.cpp
  src/USDExport.cpp
  src/MppModelExport.cpp
  src/MppModelImport.cpp
  src/ExportAll.cpp
  src/TrackResourceDocument.cpp
  src/TrackResourceSave.cpp
  src/RandomTrack.cpp
  src/TextureCache.cpp
  src/MaterialsPanel.cpp
  src/ModelsPanel.cpp
  src/ModelPlacementsPanel.cpp
  src/FileDialog.cpp
  src/Clipboard.cpp
  src/TrackPropertiesPanel.cpp
  src/PropertiesPanel.cpp
  src/ZonesPanel.cpp
  src/TriggersPanel.cpp
  src/ReservationsPanel.cpp
  src/CurvesPanel.cpp
  src/HandlingPanel.cpp
  src/RandomRangesPanel.cpp
  src/EditorIni.cpp
  src/MaterialCatalog.cpp)

add_executable(track_editor
  main.cpp
  ${EDITOR_SOURCES}
  ${IMGUI_SOURCES})

target_include_directories(track_editor PRIVATE
  include
  include/imgui
  "${TOX_MPP_GLEW_INCLUDE_DIR}"
  "${TOX_SHARED_THIRD_PARTY_INCLUDE_DIR}")

target_link_libraries(track_editor PRIVATE
  core
  Willpower::Common
  model_xml
  model_io_gltf
  SDL3::SDL3
  yaml-cpp::yaml-cpp)

add_executable(editor_track_resource_tests
  tests/track_resource_tests.cpp
  src/EditorTrackDefinition.cpp
  src/MppModelExport.cpp
  src/TrackResourceDocument.cpp
  src/TrackResourceSave.cpp
  src/FileDialog.cpp)
target_include_directories(editor_track_resource_tests PRIVATE
  include
  "${TOX_SHARED_THIRD_PARTY_INCLUDE_DIR}")
# model_io_core, not just core/Willpower::Common/model_xml: MppModelExport.cpp's export now goes
# through modelio::writeMppModelWithNamedMaterials (docs/GLTF_IMPORT_PLAN.md M4), so this target is
# no longer mpp-free either -- see mpp_model_import_tests's own target comment below, which lost
# the same property one milestone earlier.
target_link_libraries(editor_track_resource_tests PRIVATE core Willpower::Common model_xml model_io_core)
add_test(NAME editor_track_resources
  COMMAND editor_track_resource_tests "${CMAKE_SOURCE_DIR}/test-data/fixtures/path/curved-banked.json")
model_io_deploy_runtime(editor_track_resource_tests)

# Headless test for the editor's .mppmodel geometry reader/writer (TRACK_MODEL_LIST_PLAN.md
# Milestone 4). Both MppModelImport.cpp and MppModelExport.cpp are backed by cpp/model-io
# (docs/GLTF_IMPORT_PLAN.md M4, replacing M3's from-scratch byte reader/writer and the bridge
# assertion that used to pin them against model_io's contract separately) -- so this target's
# original "no Willpower/SDL/mpp dependency" premise, already weakened in M3.3, is fully retired.
add_executable(mpp_model_import_tests
  tests/mpp_model_import_tests.cpp
  src/MppModelImport.cpp
  src/MppModelExport.cpp)
target_include_directories(mpp_model_import_tests PRIVATE
  include
  "${TOX_SHARED_THIRD_PARTY_INCLUDE_DIR}")
target_link_libraries(mpp_model_import_tests PRIVATE core Willpower::Common model_xml model_io_core)
add_test(NAME mpp_model_import_tests COMMAND mpp_model_import_tests)
model_io_deploy_runtime(mpp_model_import_tests)

if (WIN32)
  # The dynamic GLEW import library, from the same build tree its header came from. If mpp is ever
  # loaded into this process too, both must share GLEW's one function-pointer table.
  target_link_directories(track_editor PRIVATE "${TOX_MPP_BUILD_DIR}/lib/$<CONFIG>")
  target_link_libraries(track_editor PRIVATE
    $<IF:$<CONFIG:Debug>,glew32d,glew32>
    opengl32 ole32 shell32 uuid)
  target_link_libraries(editor_track_resource_tests PRIVATE ole32 shell32 uuid)
  # windows.h's min/max macros (pulled in transitively via <SDL3/SDL.h> and GLEW) collide with
  # std::min/std::max and Willpower/nlohmann headers; suppress them everywhere in this target.
  target_compile_definitions(track_editor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# SDL3 (and, with multi-config generators, Willpower) leaves shared-library
# runtime dependencies in sibling output directories; copy them beside the
# executable so a direct run/F5 works, matching cpp/app's track_runner.
add_custom_command(TARGET track_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:track_editor> $<TARGET_FILE_DIR:track_editor>
  COMMAND_EXPAND_LISTS)

# mpp's libraries (and GLEW, and AssImp) are linked by raw name against MassivePolyPusher's build
# tree rather than as CMake IMPORTED targets, so $<TARGET_RUNTIME_DLLS:...> above cannot discover
# them. This copies the whole set beside the executable, exactly as cpp/model-tool does.
model_io_deploy_runtime(track_editor)

# Editor config .ini, copied beside the executable so a direct run/F5 picks it up.
add_custom_command(TARGET track_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.ini" "$<TARGET_FILE_DIR:track_editor>/editor.ini")
