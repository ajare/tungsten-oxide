# Launcher — the Windows host executable for a Willpower application DLL.
# Converted from build/vs2026/Launcher.vcxproj. The original project uses the
# GLFW path (WINDOWING_SYSTEM defaults to WINDOW_GLFW in src/Main.cpp).
set(LAUNCHER_HEADERS
  include/ApplicationDLL.h
  include/DirectoryResourceLocation.h
  include/ExitApplicationException.h
  include/ImGuiDataProvider.h
  include/Platform.h
  include/ProgramOptions.h
  include/StateManager.h
  include/Timer.h
  include/Window.h
  include/ZipResourceLocation.h
  include/glfw/ImGuiGLFW.h
  include/glfw/TimerGLFW.h
  include/glfw/WindowGLFW.h
  include/imgui/imconfig.h
  include/imgui/imgui.h
  include/imgui/imgui_internal.h
  include/imgui/implot.h
  include/imgui/implot_internal.h
  include/imgui/imstb_rectpack.h
  include/imgui/imstb_textedit.h
  include/imgui/imstb_truetype.h
  include/sdl/TimerSDL.h
  include/sdl/WindowSDL.h)

set(LAUNCHER_SOURCES
  src/ApplicationDLL.cpp
  src/DirectoryResourceLocation.cpp
  src/glfw/ImGuiGLFW.cpp
  src/glfw/TimerGLFW.cpp
  src/glfw/WindowGLFW.cpp
  src/imgui/imgui.cpp
  src/imgui/imgui_demo.cpp
  src/imgui/imgui_draw.cpp
  src/imgui/imgui_tables.cpp
  src/imgui/imgui_widgets.cpp
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

# The original launcher consumes prebuilt MassivePolyPusher, Utils, GLFW and
# SDL3 SDK libraries. Override these cache entries when they live elsewhere.
set(LAUNCHER_MPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher" CACHE PATH
  "Root of the MassivePolyPusher dependency")
set(LAUNCHER_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party" CACHE PATH
  "Root of the launcher third-party SDK headers and libraries")
set(LAUNCHER_LIB_ARCH x64 CACHE STRING "Architecture used by the launcher SDK libraries")

set(_launcher_resources)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/version/Version.rc")
  list(APPEND _launcher_resources version/Version.rc)
endif()

# WIN32 matches the vcxproj's Windows subsystem and the GLFW WinMain entry
# point. This executable is Windows-specific because it loads game DLLs with
# LoadLibrary/GetProcAddress.
add_executable(Launcher WIN32 ${LAUNCHER_SOURCES} ${LAUNCHER_HEADERS} ${_launcher_resources})
target_compile_features(Launcher PRIVATE cxx_std_20)
source_group("Header Files" FILES ${LAUNCHER_HEADERS})

target_include_directories(Launcher PRIVATE
  include
  include/imgui
  "${LAUNCHER_MPP_DIR}/mpp/include"
  "${LAUNCHER_MPP_DIR}/mpp-mesh/include"
  "${LAUNCHER_MPP_DIR}/mpp-program/include"
  "${LAUNCHER_MPP_DIR}/vendor/include"
  "${LAUNCHER_MPP_DIR}/ext/utils/include"
  "${LAUNCHER_THIRD_PARTY_DIR}/include"
  "${LAUNCHER_THIRD_PARTY_DIR}/include/fmod/core")

target_compile_definitions(Launcher PRIVATE USINGZ)

target_link_directories(Launcher PRIVATE
  "${LAUNCHER_MPP_DIR}/mpp/build/vs2026/lib/${LAUNCHER_LIB_ARCH}/$<CONFIG>"
  "${LAUNCHER_MPP_DIR}/ext/utils/build/vs2026/lib/${LAUNCHER_LIB_ARCH}/$<CONFIG>"
  "${LAUNCHER_THIRD_PARTY_DIR}/lib/vs2026/${LAUNCHER_LIB_ARCH}/$<CONFIG>")
target_link_libraries(Launcher PRIVATE
  Willpower.Application
  Willpower.Common
  SDL3
  glfw3
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
  message(WARNING "Launcher uses the Windows-only WinMain/LoadLibrary implementation.")
endif()

# CMake-managed target DLLs (not the legacy raw import libraries above) are
# copied beside the executable, replacing the vcxproj's CopyWillpowerBinaries
# post-build batch script for the targets CMake can identify.
if (WIN32 AND WILLPOWER_BUILD_SHARED_LIBS)
  add_custom_command(TARGET Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:Launcher> $<TARGET_FILE_DIR:Launcher>
    COMMAND_EXPAND_LISTS)
endif()
