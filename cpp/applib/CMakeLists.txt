# AppLib — shared gameplay/entity/state primitives used by BooleanWorld.
# Converted from build/vs2026/AppLib.vcxproj. Several original Willpower
# feature libraries (Collide, Firepower, Serialization and Viz) remain external
# to this checkout and are retained as prebuilt-library link dependencies.
set(APPLIB_HEADERS
  include/applib/Entity.h
  include/applib/EntityFacadeFactory.h
  include/applib/EntityHandler.h
  include/applib/EntityManager.h
  include/applib/EntityProperties.h
  include/applib/EntityFacade.h
  include/applib/Exceptions.h
  include/applib/Game.h
  include/applib/GameDefaultDefinitionFactory.h
  include/applib/GameResourceDefinitionFactory.h
  include/applib/Map.h
  include/applib/MapDefaultDefinitionFactory.h
  include/applib/MapGeometryObjectAttributes.h
  include/applib/MapResourceDefinitionFactory.h
  include/applib/MapTransitionData.h
  include/applib/Model.h
  include/applib/ModelInstance.h
  include/applib/ObjectArray.h
  include/applib/PhysicalStats.h
  include/applib/Platform.h
  include/applib/ProtoEntity.h
  include/applib/ProtoEntityDefaultDefinitionFactory.h
  include/applib/ProtoEntityResourceDefinitionFactory.h
  include/applib/ScreenFxManager.h
  include/applib/State.h
  include/applib/StateController.h
  include/applib/StateLoad.h
  include/applib/StateMapLoad.h
  include/applib/StateMapTransition.h
  include/applib/StateMapUnload.h
  include/applib/StatePlay.h
  include/applib/StateTransitionData.h
  include/applib/StateUnload.h
  include/applib/ThreadableLoadState.h)

set(APPLIB_SOURCES
  src/Entity.cpp
  src/EntityHandler.cpp
  src/EntityManager.cpp
  src/EntityFacade.cpp
  src/Game.cpp
  src/GameDefaultDefinitionFactory.cpp
  src/GameResourceDefinitionFactory.cpp
  src/Map.cpp
  src/MapDefaultDefinitionFactory.cpp
  src/MapResourceDefinitionFactory.cpp
  src/ModelInstance.cpp
  src/ProtoEntity.cpp
  src/ProtoEntityDefaultDefinitionFactory.cpp
  src/ProtoEntityResourceDefinitionFactory.cpp
  src/ScreenFxManager.cpp
  src/State.cpp
  src/StateController.cpp
  src/StateLoad.cpp
  src/StateMapLoad.cpp
  src/StateMapTransition.cpp
  src/StateMapUnload.cpp
  src/StatePlay.cpp
  src/StateUnload.cpp
  src/ThreadableLoadState.cpp)

set(APPLIB_MPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher" CACHE PATH
  "Root of the MassivePolyPusher dependency")
set(_applib_third_party_dir "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")
# CACHE variables retain their first configured value. Migrate build trees made
# before third_party/ moved from cpp/willpower/ to cpp/ without overriding an
# intentional user-supplied SDK location.
set(_applib_legacy_third_party_dir "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/third_party")
if (APPLIB_THIRD_PARTY_DIR STREQUAL _applib_legacy_third_party_dir)
  set(APPLIB_THIRD_PARTY_DIR "${_applib_third_party_dir}" CACHE PATH
    "Root of the AppLib third-party SDK headers and libraries" FORCE)
else()
  set(APPLIB_THIRD_PARTY_DIR "${_applib_third_party_dir}" CACHE PATH
    "Root of the AppLib third-party SDK headers and libraries")
endif()
unset(_applib_legacy_third_party_dir)
unset(_applib_third_party_dir)
set(APPLIB_LIB_ARCH x64 CACHE STRING "Architecture used by the AppLib SDK libraries")

add_library(AppLib SHARED ${APPLIB_SOURCES} ${APPLIB_HEADERS})
add_library(AppLib::AppLib ALIAS AppLib)
source_group("Header Files" FILES ${APPLIB_HEADERS})
target_compile_features(AppLib PUBLIC cxx_std_23)
target_compile_definitions(AppLib PRIVATE
  APPLIB_DLL_EXPORT
  _SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
  _SILENCE_CXX23_DENORM_DEPRECATION_WARNING)
set_target_properties(AppLib PROPERTIES DEBUG_POSTFIX d)

target_include_directories(AppLib PUBLIC include)
target_include_directories(AppLib PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include/applib"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.application/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.common/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/willpower.collide/include"
  "${APPLIB_MPP_DIR}/mpp/include"
  "${APPLIB_MPP_DIR}/mpp-helper/include"
  "${APPLIB_MPP_DIR}/mpp-mesh/include"
  "${APPLIB_MPP_DIR}/mpp-program/include"
  "${APPLIB_MPP_DIR}/vendor/include"
  "${APPLIB_MPP_DIR}/ext/utils/include"
  "${APPLIB_THIRD_PARTY_DIR}/include"
  "${APPLIB_THIRD_PARTY_DIR}/include/fmod/core")

target_link_directories(AppLib PRIVATE
  "${APPLIB_MPP_DIR}/mpp/build/vs2026/lib/${APPLIB_LIB_ARCH}/$<CONFIG>"
  "${APPLIB_MPP_DIR}/mpp-helper/build/vs2026/lib/${APPLIB_LIB_ARCH}/$<CONFIG>"
  "${APPLIB_MPP_DIR}/mpp-mesh/build/vs2026/lib/${APPLIB_LIB_ARCH}/$<CONFIG>"
  "${APPLIB_MPP_DIR}/ext/utils/build/vs2026/lib/${APPLIB_LIB_ARCH}/$<CONFIG>"
  "${APPLIB_THIRD_PARTY_DIR}/lib/vs2026/${APPLIB_LIB_ARCH}/$<CONFIG>")
target_link_libraries(AppLib PRIVATE
  Willpower.Application
  Willpower.Common
  Willpower.Geometry
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,MppHelperd,MppHelper>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>)

if (MSVC)
  target_compile_options(AppLib PRIVATE /W3)
endif()
if (WIN32)
  target_compile_definitions(AppLib PRIVATE UNICODE _UNICODE)
else()
  message(WARNING "AppLib was authored for the Windows Willpower stack.")
endif()
