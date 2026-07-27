# BooleanWorld — the Windows application DLL loaded by Launcher.
# Converted from build/vs2026/BooleanWorld.vcxproj. It relies on the legacy
# AppLib, Willpower Collide/Viz/Serialization, MassivePolyPusher, Utils and
# yaml-cpp dependencies, which are not all present in this checkout.
set(BOOLEAN_WORLD_HEADERS
  include/BooleanWorldModel.h
  include/ClippingRecord.h
  include/DebugLineDataProvider.h
  include/DisplayMessage.h
  include/EntityHandlerBooleanWorld.h
  include/EntityProperties.h
  include/EntityStats.h
  include/EntityType.h
  include/Game.h
  include/GameDefinitionFactory.h
  include/GameException.h
  include/Map.h
  include/MapBooleanWorldDefinitionFactory.h
  include/MapException.h
  include/MetricsDataProvider.h
  include/Platform.h
  include/PrimitiveLineDataProvider.h
  include/ProtoEntity.h
  include/ProtoEntityDefinitionFactory.h
  include/ReactiveCamera.h
  include/ScrollingBuffer.h
  include/StateControllerBooleanWorld.h
  include/StateMapLoadBooleanWorld.h
  include/StateMapTransitionBooleanWorld.h
  include/StateMapUnloadBooleanWorld.h
  include/StatePlayBooleanWorld.h
  include/StatePlayTransitionData.h
  include/TriMeshDataProvider.h
  include/TriMeshEntityFacadeFactory.h
  include/WorldBatch.h
  include/WorldBatchRenderer.h
  include/WorldBorderDataProvider.h
  include/WorldCollisionSim.h
  include/WorldRenderer.h
  include/WorldRenderer3d.h
  include/WorldTriangle.h
  include/WorldTriangle2dDataProvider.h
  include/WorldTriangle3dDataProvider.h
  include/imgui/imconfig.h
  include/imgui/imgui.h
  include/imgui/imgui_internal.h
  include/imgui/implot.h
  include/imgui/implot_internal.h
  include/imgui/imstb_rectpack.h
  include/imgui/imstb_textedit.h
  include/imgui/imstb_truetype.h)

set(BOOLEAN_WORLD_SOURCES
  src/DLL.cpp
  src/EntityHandlerBooleanWorld.cpp
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
  src/MapBooleanWorldDefinitionFactory.cpp
  src/ProtoEntity.cpp
  src/ProtoEntityDefinitionFactory.cpp
  src/ReactiveCamera.cpp
  src/StateControllerBooleanWorld.cpp
  src/StateMapLoadBooleanWorld.cpp
  src/StateMapTransitionBooleanWorld.cpp
  src/StateMapUnloadBooleanWorld.cpp
  src/StatePlayBooleanWorld.cpp
  src/WorldBatch.cpp
  src/WorldBatchRenderer.cpp
  src/WorldCollisionSim.cpp
  src/WorldRenderer.cpp
  src/WorldTriangle3dDataProvider.cpp
  src/WorldRenderer3d.cpp)

set(BOOLEAN_WORLD_MPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher" CACHE PATH
  "Root of the MassivePolyPusher dependency")
# AppLib is built by the combined cpp/ project before this directory; retain a
# cache override only for standalone/legacy layouts.
set(BOOLEAN_WORLD_APPLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../applib" CACHE PATH
  "Root of the AppLib dependency")
set(BOOLEAN_WORLD_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party" CACHE PATH
  "Root of the BooleanWorld third-party SDK headers and libraries")
set(BOOLEAN_WORLD_LIB_ARCH x64 CACHE STRING "Architecture used by the BooleanWorld SDK libraries")

add_library(BooleanWorld SHARED ${BOOLEAN_WORLD_SOURCES} ${BOOLEAN_WORLD_HEADERS})
source_group("Header Files" FILES ${BOOLEAN_WORLD_HEADERS})
target_compile_features(BooleanWorld PRIVATE cxx_std_23)
target_compile_definitions(BooleanWorld PRIVATE
  APPLICATION_DLL_EXPORT
  WP_USE_ASSERT_TRACE
  YAML_CPP_STATIC_DEFINE
  $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)
set_target_properties(BooleanWorld PROPERTIES DEBUG_POSTFIX d)

target_include_directories(BooleanWorld PRIVATE
  include
  include/imgui
  "${CMAKE_CURRENT_SOURCE_DIR}/../core/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common/include"
  "${BOOLEAN_WORLD_APPLIB_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.application/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.common/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.geometry/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.collide/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.serialization/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.viz/include"
  "${BOOLEAN_WORLD_MPP_DIR}/mpp/include"
  "${BOOLEAN_WORLD_MPP_DIR}/mpp-helper/include"
  "${BOOLEAN_WORLD_MPP_DIR}/mpp-mesh/include"
  "${BOOLEAN_WORLD_MPP_DIR}/mpp-program/include"
  "${BOOLEAN_WORLD_MPP_DIR}/vendor/include"
  "${BOOLEAN_WORLD_MPP_DIR}/ext/utils/include"
  "${BOOLEAN_WORLD_THIRD_PARTY_DIR}/include")

target_link_directories(BooleanWorld PRIVATE
  "${BOOLEAN_WORLD_MPP_DIR}/mpp/build/vs2026/lib/${BOOLEAN_WORLD_LIB_ARCH}/$<CONFIG>"
  "${BOOLEAN_WORLD_MPP_DIR}/mpp-helper/build/vs2026/lib/${BOOLEAN_WORLD_LIB_ARCH}/$<CONFIG>"
  "${BOOLEAN_WORLD_MPP_DIR}/mpp-mesh/build/vs2026/lib/${BOOLEAN_WORLD_LIB_ARCH}/$<CONFIG>"
  "${BOOLEAN_WORLD_MPP_DIR}/ext/utils/build/vs2026/lib/${BOOLEAN_WORLD_LIB_ARCH}/$<CONFIG>"
  "${BOOLEAN_WORLD_THIRD_PARTY_DIR}/lib/vs2026/${BOOLEAN_WORLD_LIB_ARCH}/$<CONFIG>")
target_link_libraries(BooleanWorld PRIVATE
  core
  Willpower.Application
  Willpower.Common
  Willpower.Geometry
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  $<IF:$<CONFIG:Debug>,MppHelperd,MppHelper>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>
  $<IF:$<CONFIG:Debug>,Willpower.Collided,Willpower.Collide>
  $<IF:$<CONFIG:Debug>,Willpower.Serializationd,Willpower.Serialization>
  $<IF:$<CONFIG:Debug>,Willpower.Vizd,Willpower.Viz>
  AppLib::AppLib
  $<IF:$<CONFIG:Debug>,yaml-cppd,yaml-cpp>)

if (MSVC)
  target_compile_options(BooleanWorld PRIVATE /W4)
endif()
if (WIN32)
  target_compile_definitions(BooleanWorld PRIVATE UNICODE _UNICODE)
else()
  message(WARNING "BooleanWorld is a Windows application DLL and has not been ported off Windows.")
endif()
