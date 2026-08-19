# Launcher — the Windows SDL3 host executable for a Willpower application DLL.
# Converted from build/vs2026/Launcher.vcxproj.
set(LAUNCHER_HEADERS
  include/ApplicationDLL.h
  include/ExitApplicationException.h
  include/ImGuiDataProvider.h
  include/Platform.h
  include/ProgramOptions.h
  include/StateManager.h
  include/Timer.h
  include/Window.h
  include/ZipResourceLocation.h
  include/imgui/imconfig.h
  include/imgui/imgui.h
  include/imgui/imgui_internal.h
  include/imgui/imgui_impl_sdl3.h
  include/imgui/implot.h
  include/imgui/implot_internal.h
  include/imgui/imstb_rectpack.h
  include/imgui/imstb_textedit.h
  include/imgui/imstb_truetype.h
  include/sdl/TimerSDL.h
  include/sdl/WindowSDL.h)

set(LAUNCHER_SOURCES
  src/ApplicationDLL.cpp
  src/imgui/imgui.cpp
  src/imgui/imgui_demo.cpp
  src/imgui/imgui_draw.cpp
  src/imgui/imgui_tables.cpp
  src/imgui/imgui_widgets.cpp
  src/imgui/imgui_impl_sdl3.cpp
  src/imgui/implot.cpp
  src/imgui/implot_demo.cpp
  src/imgui/implot_items.cpp
  src/Main.cpp
  src/ProgramOptions.cpp
  src/sdl/TimerSDL.cpp
  src/sdl/WindowSDL.cpp
  src/StateManager.cpp
  src/Timer.cpp
  src/Window.cpp
  src/ZipResourceLocation.cpp)

# The launcher consumes prebuilt MassivePolyPusher and Utils libraries. SDL3
# comes from MassivePolyPusher's pinned source submodule.
set(_launcher_legacy_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher")
set(_launcher_repository_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massive-poly-pusher")
set(_launcher_cpp_willpower_mpp_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/ext/massive-poly-pusher")
set(_launcher_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/willpower/ext/massive-poly-pusher")
if (LAUNCHER_MPP_DIR STREQUAL _launcher_legacy_mpp_dir OR
    LAUNCHER_MPP_DIR STREQUAL _launcher_repository_mpp_dir OR
    LAUNCHER_MPP_DIR STREQUAL _launcher_cpp_willpower_mpp_dir)
  set(LAUNCHER_MPP_DIR "${_launcher_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency" FORCE)
else()
  set(LAUNCHER_MPP_DIR "${_launcher_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency")
endif()
unset(_launcher_legacy_mpp_dir)
unset(_launcher_repository_mpp_dir)
unset(_launcher_cpp_willpower_mpp_dir)
unset(_launcher_mpp_dir)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/MppBuildTree.cmake")
tox_resolve_mpp_build_tree("${LAUNCHER_MPP_DIR}")
set(_launcher_third_party_dir "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")
set(_launcher_legacy_third_party_dir "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/third_party")
if (LAUNCHER_THIRD_PARTY_DIR STREQUAL _launcher_legacy_third_party_dir)
  set(LAUNCHER_THIRD_PARTY_DIR "${_launcher_third_party_dir}" CACHE PATH
    "Root of the launcher third-party SDK headers and libraries" FORCE)
else()
  set(LAUNCHER_THIRD_PARTY_DIR "${_launcher_third_party_dir}" CACHE PATH
    "Root of the launcher third-party SDK headers and libraries")
endif()
unset(_launcher_legacy_third_party_dir)
unset(_launcher_third_party_dir)
set(LAUNCHER_LIB_ARCH x64 CACHE STRING "Architecture used by the launcher SDK libraries")

set(_launcher_resources)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/version/Version.rc")
  list(APPEND _launcher_resources version/Version.rc)
endif()

# WIN32 matches the vcxproj's Windows subsystem; SDL provides the platform
# entry point. This executable is Windows-specific because it loads game DLLs with
# LoadLibrary/GetProcAddress.
add_executable(Launcher WIN32 ${LAUNCHER_SOURCES} ${LAUNCHER_HEADERS} ${_launcher_resources})
if (COMMAND tox_copy_asan_runtime)
  tox_copy_asan_runtime(Launcher)
endif()
target_compile_features(Launcher PRIVATE cxx_std_20)
# AppLib is not a link dependency of Launcher, but its DLL is loaded by the
# application DLL and must sit beside Launcher at runtime.
if (TARGET AppLib)
  add_dependencies(Launcher AppLib)
  set(LAUNCHER_CMAKE_RUNTIME_FILES
    $<TARGET_RUNTIME_DLLS:Launcher>
    $<TARGET_FILE:AppLib>)
else()
  set(LAUNCHER_CMAKE_RUNTIME_FILES $<TARGET_RUNTIME_DLLS:Launcher>)
endif()
# Match the Debugging settings from the legacy Visual Studio launcher project.
# Start every configuration from its own output directory.
set_target_properties(Launcher PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)"
  VS_DEBUGGER_COMMAND_ARGUMENTS "$<$<OR:$<CONFIG:Debug>,$<CONFIG:MemCheck>>:Game.yaml>")
source_group("Header Files" FILES ${LAUNCHER_HEADERS})

target_include_directories(Launcher PRIVATE
  include
  include/imgui
  "${LAUNCHER_MPP_DIR}/mpp/include"
  "${LAUNCHER_MPP_DIR}/mpp-mesh/include"
  "${LAUNCHER_MPP_DIR}/mpp-program/include"
  "${LAUNCHER_MPP_DIR}/vendor/include"
  "${TOX_MPP_GLEW_INCLUDE_DIR}"
  "${LAUNCHER_MPP_DIR}/ext/utils/include"
  "${LAUNCHER_THIRD_PARTY_DIR}/include")

target_compile_definitions(Launcher PRIVATE USINGZ)

target_link_directories(Launcher PRIVATE
  "${TOX_MPP_BUILD_DIR}/lib/$<CONFIG>")
if (WIN32)
  target_link_directories(Launcher PRIVATE
    "${LAUNCHER_THIRD_PARTY_DIR}/lib/vs2026/${LAUNCHER_LIB_ARCH}/$<CONFIG>")
endif()
target_link_libraries(Launcher PRIVATE
  Willpower.Application
  Willpower.Common
  SDL3::SDL3
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>)

if (MSVC)
  target_compile_options(Launcher PRIVATE /W3)
endif()
if (WIN32)
  # Matches the vcxproj's <CharacterSet>Unicode</CharacterSet>, making the
  # LoadLibrary macro resolve to LoadLibraryW for the std::wstring argument.
  target_compile_definitions(Launcher PRIVATE UNICODE _UNICODE)
else()
  message(WARNING
    "Launcher uses Windows application DLL loading; its platform guards intentionally fail "
    "compilation off Windows.")
endif()

# Runtime deployment, converted from build/CopyWillpowerBinaries.bat. The
# batch file copied the machine-specific Launcher support files, every bundled
# third-party runtime file, and DLLs from each MassivePolyPusher component.
# Do the glob at *build* time in a generated script: wrapping a CMake list in a
# configuration generator expression expands it into invalid `$<1:...>` /
# `$<0:...>` command arguments in Visual Studio projects.
set(LAUNCHER_SUPPORT_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/support/$ENV{COMPUTERNAME}/vs2026/${LAUNCHER_LIB_ARCH}")
set(_launcher_copy_runtime_script [=[
if (NOT DEFINED OUTPUT_DIR OR NOT DEFINED CONFIG)
  message(FATAL_ERROR "OUTPUT_DIR and CONFIG are required")
endif()

if (NOT DEFINED MPP_CONFIG)
  set(MPP_CONFIG "${CONFIG}")
endif()
set(_runtime_roots
  "@LAUNCHER_SUPPORT_ROOT@/${CONFIG}"
  "@LAUNCHER_THIRD_PARTY_DIR@/bin/vs2026/@LAUNCHER_LIB_ARCH@/${CONFIG}"
  "@TOX_MPP_BUILD_DIR@/bin/${MPP_CONFIG}")

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

# One checked-in Game launcher config per build type (support/), deployed under the name Main.cpp
# looks for -- see CMakeLists.txt's VS_DEBUGGER_COMMAND_ARGUMENTS.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "@CMAKE_CURRENT_SOURCE_DIR@/support/Game.${CONFIG}.yaml" "${OUTPUT_DIR}/Game.yaml"
  COMMAND_ERROR_IS_FATAL ANY)
]=])
file(CONFIGURE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/CopyLauncherRuntime.cmake"
  CONTENT "${_launcher_copy_runtime_script}"
  @ONLY)
unset(_launcher_copy_runtime_script)

if (WIN32)
  add_custom_command(TARGET Launcher POST_BUILD
    # CMake targets (Willpower.Application/Common and any target dependencies)
    # replace the batch script's per-Willpower-DLL copy commands.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${LAUNCHER_CMAKE_RUNTIME_FILES} "$<TARGET_FILE_DIR:Launcher>"
    # The generated script skips missing optional roots and copies all files
    # from the support/vendor/MPP directories that do exist for this config.
    COMMAND ${CMAKE_COMMAND}
      "-DOUTPUT_DIR=$<TARGET_FILE_DIR:Launcher>"
      "-DCONFIG=$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:MemCheck>>,Debug,Release>"
      "-DMPP_CONFIG=$<CONFIG>"
      -P "${CMAKE_CURRENT_BINARY_DIR}/CopyLauncherRuntime.cmake"
    COMMAND_EXPAND_LISTS)
endif()
