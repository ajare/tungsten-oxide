# TungstenMonoxide — the Windows application DLL loaded by Launcher.
# Converted from build/vs2026/TungstenMonoxide.vcxproj. It relies on AppLib,
# Willpower, MassivePolyPusher, Utils, and yaml-cpp.
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
  include/PbrMeshSpecification.h
  include/PbrVertexConversion.h
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
  include/TungstenPbrPackage.h
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
  src/PbrMeshSpecification.cpp
  src/PbrVertexConversion.cpp
  src/ProtoEntity.cpp
  src/ProtoEntityDefinitionFactory.cpp
  src/ReactiveCamera.cpp
  src/StateControllerTungstenMonoxide.cpp
  src/StateMapLoadTungstenMonoxide.cpp
  src/StateMapTransitionTungstenMonoxide.cpp
  src/StateMapUnloadTungstenMonoxide.cpp
  src/StatePlayTungstenMonoxide.cpp
  src/TrackCollisionBuild.cpp
  src/TungstenPbrPackage.cpp)

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
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/MppBuildTree.cmake")
tox_resolve_mpp_build_tree("${TUNGSTEN_MONOXIDE_MPP_DIR}")
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
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-data/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-helper/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-app-support/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-resource-parsers/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-mesh/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/mpp-program/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/vendor/include"
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/ext/utils/include"
  "${TUNGSTEN_MONOXIDE_THIRD_PARTY_DIR}/include")

target_link_directories(TungstenMonoxide PRIVATE
  "${TOX_MPP_BUILD_DIR}/lib/$<CONFIG>")
if (WIN32)
  target_link_directories(TungstenMonoxide PRIVATE
    "${TUNGSTEN_MONOXIDE_THIRD_PARTY_DIR}/lib/vs2026/${TUNGSTEN_MONOXIDE_LIB_ARCH}/$<CONFIG>")
endif()
target_link_libraries(TungstenMonoxide PRIVATE
  core
  Willpower.Application
  Willpower.Common
  Willpower.Geometry
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  # TungstenPbrPackage holds an mpp::PbrPipelineDocument, whose PbrPipelineResourceDocument::
  # definition is now an mpp::data::StructuredData (MassivePolyPusher's "Decouple public headers
  # from implementation dependencies" migration) -- constructing/copying/destroying one needs this
  # library's exported special members.
  $<IF:$<CONFIG:Debug>,MppDatad,MppData>
  $<IF:$<CONFIG:Debug>,MppHelperd,MppHelper>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,MppResourceParsersd,MppResourceParsers>
  $<IF:$<CONFIG:Debug>,MppAppSupportd,MppAppSupport>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>
  AppLib::AppLib
  yaml-cpp::yaml-cpp)

# Guard stable embedded material-key mappings without creating a renderer.
add_executable(tungsten_monoxide_pbr_material_binding_mapping_tests
  tests/pbr_material_binding_mapping_tests.cpp)
target_compile_features(tungsten_monoxide_pbr_material_binding_mapping_tests PRIVATE cxx_std_20)
target_include_directories(tungsten_monoxide_pbr_material_binding_mapping_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.common/include")
target_link_libraries(tungsten_monoxide_pbr_material_binding_mapping_tests PRIVATE Willpower.Common yaml-cpp::yaml-cpp)
add_test(NAME tungsten_monoxide_pbr_material_bindings
  COMMAND tungsten_monoxide_pbr_material_binding_mapping_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/NewTrack.mppmodel"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/Resources.yaml")

add_executable(tungsten_monoxide_pbr_vertex_conversion_tests
  tests/pbr_vertex_conversion_tests.cpp
  src/PbrVertexConversion.cpp)
target_compile_features(tungsten_monoxide_pbr_vertex_conversion_tests PRIVATE cxx_std_20)
target_include_directories(tungsten_monoxide_pbr_vertex_conversion_tests PRIVATE
  include
  "${TUNGSTEN_MONOXIDE_MPP_DIR}/vendor/include")
add_test(NAME tungsten_monoxide_pbr_vertex_conversion
  COMMAND tungsten_monoxide_pbr_vertex_conversion_tests)

# Milestone 1 package validation is intentionally independent of MPP DLLs so it can
# guard the committed PipelineEditor export before runtime integration is added.
find_package(Python3 QUIET COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
  add_test(NAME tungsten_monoxide_pbr_package
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/pbr/validate_package.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/TungstenMonoxide.mpppackage")
endif()

if (WIN32 AND (BUILD_SHARED_LIBS OR WILLPOWER_BUILD_SHARED_LIBS))
  add_custom_command(TARGET tungsten_monoxide_pbr_material_binding_mapping_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:tungsten_monoxide_pbr_material_binding_mapping_tests>
      $<TARGET_FILE_DIR:tungsten_monoxide_pbr_material_binding_mapping_tests>
    COMMAND_EXPAND_LISTS)
endif()

if (MSVC)
  target_compile_options(TungstenMonoxide PRIVATE /W4)
  target_compile_options(tungsten_monoxide_pbr_material_binding_mapping_tests PRIVATE /W4)
  target_compile_options(tungsten_monoxide_pbr_vertex_conversion_tests PRIVATE /W4)
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
  message(WARNING
    "TungstenMonoxide is a Windows application DLL; its platform guards intentionally fail "
    "compilation off Windows.")
endif()
