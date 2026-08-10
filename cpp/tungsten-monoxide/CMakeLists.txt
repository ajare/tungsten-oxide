# TungstenMonoxide — the Windows application DLL loaded by Launcher.
# Converted from build/vs2026/TungstenMonoxide.vcxproj. It relies on the legacy
# AppLib, Willpower Collide/Viz/Serialization, MassivePolyPusher, Utils and
# yaml-cpp dependencies, which are not all present in this checkout.
set(TUNGSTEN_MONOXIDE_HEADERS
  include/TungstenMonoxideModel.h
  include/DebugLineDataProvider.h
  include/DisplayMessage.h
  include/EntityHandlerTungstenMonoxide.h
  include/EntityProperties.h
  include/EntityStats.h
  include/EntityType.h
  include/Game.h
  include/GameDefinitionFactory.h
  include/GameException.h
  include/Map.h
  include/MapTungstenMonoxideDefinitionFactory.h
  include/MapException.h
  include/MetricsDataProvider.h
  include/Platform.h
  include/ProtoEntity.h
  include/ProtoEntityDefinitionFactory.h
  include/ReactiveCamera.h
  include/ScrollingBuffer.h
  include/StateControllerTungstenMonoxide.h
  include/StateMapLoadTungstenMonoxide.h
  include/StateMapTransitionTungstenMonoxide.h
  include/StateMapUnloadTungstenMonoxide.h
  include/StatePlayTungstenMonoxide.h
  include/TrackCollisionBuild.h
  include/imgui/imconfig.h
  include/imgui/imgui.h
  include/imgui/imgui_internal.h
  include/imgui/implot.h
  include/imgui/implot_internal.h
  include/imgui/imstb_rectpack.h
  include/imgui/imstb_textedit.h
  include/imgui/imstb_truetype.h)

set(TUNGSTEN_MONOXIDE_SOURCES
  src/DLL.cpp
  src/EntityHandlerTungstenMonoxide.cpp
  src/Game.cpp
  src/GameDefinitionFactory.cpp
  src/imgui/imgui.cpp
  src/imgui/imgui_demo.cpp
  src/imgui/imgui_draw.cpp
  src/imgui/imgui_tables.cpp
  src/imgui/imgui_widgets.cpp
  src/imgui/implot.cpp
  src/imgui/implot_demo.cpp
  src/imgui/implot_items.cpp
  src/Map.cpp
  src/MapTungstenMonoxideDefinitionFactory.cpp
  src/ProtoEntity.cpp
  src/ProtoEntityDefinitionFactory.cpp
  src/ReactiveCamera.cpp
  src/StateControllerTungstenMonoxide.cpp
  src/StateMapLoadTungstenMonoxide.cpp
  src/StateMapTransitionTungstenMonoxide.cpp
  src/StateMapUnloadTungstenMonoxide.cpp
  src/StatePlayTungstenMonoxide.cpp
  src/TrackCollisionBuild.cpp)

set(_tungsten_monoxide_legacy_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher")
set(_tungsten_monoxide_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massive-poly-pusher")
if (TUNGSTEN_MONOXIDE_MPP_DIR STREQUAL _tungsten_monoxide_legacy_mpp_dir)
  set(TUNGSTEN_MONOXIDE_MPP_DIR "${_tungsten_monoxide_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency" FORCE)
else()
  set(TUNGSTEN_MONOXIDE_MPP_DIR "${_tungsten_monoxide_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency")
endif()
unset(_tungsten_monoxide_legacy_mpp_dir)
unset(_tungsten_monoxide_mpp_dir)
# AppLib is built by the combined cpp/ project before this directory; retain a
# cache override only for standalone/legacy layouts.
set(TUNGSTEN_MONOXIDE_APPLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../applib" CACHE PATH
  "Root of the AppLib dependency")
set(_tungsten_monoxide_third_party_dir "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")
set(_tungsten_monoxide_legacy_third_party_dir "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/third_party")
if (TUNGSTEN_MONOXIDE_THIRD_PARTY_DIR STREQUAL _tungsten_monoxide_legacy_third_party_dir)
  set(TUNGSTEN_MONOXIDE_THIRD_PARTY_DIR "${_tungsten_monoxide_third_party_dir}" CACHE PATH
    "Root of the TungstenMonoxide third-party SDK headers and libraries" FORCE)
else()
  set(TUNGSTEN_MONOXIDE_THIRD_PARTY_DIR "${_tungsten_monoxide_third_party_dir}" CACHE PATH
    "Root of the TungstenMonoxide third-party SDK headers and libraries")
endif()
unset(_tungsten_monoxide_legacy_third_party_dir)
unset(_tungsten_monoxide_third_party_dir)
set(TUNGSTEN_MONOXIDE_LIB_ARCH x64 CACHE STRING "Architecture used by the TungstenMonoxide SDK libraries")

add_library(TungstenMonoxide SHARED ${TUNGSTEN_MONOXIDE_SOURCES} ${TUNGSTEN_MONOXIDE_HEADERS})
source_group("Header Files" FILES ${TUNGSTEN_MONOXIDE_HEADERS})
target_compile_features(TungstenMonoxide PRIVATE cxx_std_23)
target_compile_definitions(TungstenMonoxide PRIVATE
  APPLICATION_DLL_EXPORT
  WP_USE_ASSERT_TRACE
  YAML_CPP_STATIC_DEFINE
  $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)
set_target_properties(TungstenMonoxide PROPERTIES DEBUG_POSTFIX d)

target_include_directories(TungstenMonoxide PRIVATE
  include
  include/imgui
  "${CMAKE_CURRENT_SOURCE_DIR}/../core/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common/include"
  "${TUNGSTEN_MONOXIDE_APPLIB_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.application/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.common/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.geometry/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-helper/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-mesh/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-program/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/vendor/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/build/cmake/_deps/glew-2.3.1/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/ext/utils/include"
  "${TUNGSTEN_MONOXIDE_THIRD_PARTY_DIR}/include"
  "${TUNGSTEN_MONOXIDE_THIRD_PARTY_DIR}/include/fmod/core")

target_link_directories(TungstenMonoxide PRIVATE
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/build/cmake/lib/$<CONFIG>"
  "${TUNGSTEN_MONOXIDE_THIRD_PARTY_DIR}/lib/vs2026/${TUNGSTEN_MONOXIDE_LIB_ARCH}/$<CONFIG>")
target_link_libraries(TungstenMonoxide PRIVATE
  core
  Willpower.Application
  Willpower.Common
  Willpower.Geometry
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  $<IF:$<CONFIG:Debug>,MppHelperd,MppHelper>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>
  AppLib::AppLib
  $<IF:$<CONFIG:Debug>,yaml-cppd,yaml-cpp>)

if (MSVC)
  target_compile_options(TungstenMonoxide PRIVATE /W4)
endif()
if (WIN32)
  target_compile_definitions(TungstenMonoxide PRIVATE UNICODE _UNICODE)

  # Launcher loads this application DLL dynamically, so CMake cannot discover
  # its runtime dependencies from Launcher's link graph. Keep the runnable
  # launcher directory complete after every application build.
  if (TARGET Launcher)
    add_custom_command(TARGET TungstenMonoxide POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Launcher>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:TungstenMonoxide>"
        $<TARGET_RUNTIME_DLLS:TungstenMonoxide>
        "$<TARGET_FILE_DIR:Launcher>"
      COMMAND_EXPAND_LISTS)
  endif()
else()
  message(WARNING "TungstenMonoxide is a Windows application DLL and has not been ported off Windows.")
endif()
