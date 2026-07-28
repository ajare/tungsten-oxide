# Willpower.Application — resource loading, audio, input, state and scheduling
# primitives for applications using the Willpower rendering stack.
#
# This target is a direct CMake conversion of build/vs2026/
# Willpower.Application.vcxproj. It retains the original dependency on the
# MassivePolyPusher, FreeImage and FMOD binary SDKs.
set(WILLPOWER_APPLICATION_HEADERS
  include/willpower/application/ApplicationSettings.h
  include/willpower/application/AudioOptions.h
  include/willpower/application/AudioSystem.h
  include/willpower/application/Document.h
  include/willpower/application/DocumentManager.h
  include/willpower/application/InputHelper.h
  include/willpower/application/InputStateManager.h
  include/willpower/application/Key.h
  include/willpower/application/MouseButton.h
  include/willpower/application/Platform.h
  include/willpower/application/Scheduler.h
  include/willpower/application/SchedulerTask.h
  include/willpower/application/ServiceLocator.h
  include/willpower/application/State.h
  include/willpower/application/StateExceptions.h
  include/willpower/application/StateFactory.h
  include/willpower/application/resourcesystem/AnimationSetDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/AnimationSetResource.h
  include/willpower/application/resourcesystem/AnimationSetResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/AudioBankDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/AudioBankResource.h
  include/willpower/application/resourcesystem/AudioBankResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/DataStream.h
  include/willpower/application/resourcesystem/DirectoryResourceLocation.h
  include/willpower/application/resourcesystem/ImageDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/ImageResource.h
  include/willpower/application/resourcesystem/ImageResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/ImageSetDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/ImageSetResource.h
  include/willpower/application/resourcesystem/ImageSetResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/MaterialDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/MaterialResource.h
  include/willpower/application/resourcesystem/MaterialResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/ProgramDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/ProgramResource.h
  include/willpower/application/resourcesystem/ProgramResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/Resource.h
  include/willpower/application/resourcesystem/ResourceCallback.h
  include/willpower/application/resourcesystem/ResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/ResourceExceptions.h
  include/willpower/application/resourcesystem/ResourceFactory.h
  include/willpower/application/resourcesystem/ResourceLocation.h
  include/willpower/application/resourcesystem/ResourceManager.h
  include/willpower/application/resourcesystem/ResourceRecord.h
  include/willpower/application/resourcesystem/ShaderDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/ShaderResource.h
  include/willpower/application/resourcesystem/ShaderResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/TextFileDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/TextFileResource.h
  include/willpower/application/resourcesystem/TextFileResourceDefinitionFactory.h
  include/willpower/application/resourcesystem/XmlFileDefaultDefinitionFactory.h
  include/willpower/application/resourcesystem/XmlFileResource.h
  include/willpower/application/resourcesystem/XmlFileResourceDefinitionFactory.h)

set(WILLPOWER_APPLICATION_SOURCES
  src/ApplicationSettings.cpp
  src/AudioSystem.cpp
  src/InputHelper.cpp
  src/InputStateManager.cpp
  src/Scheduler.cpp
  src/SchedulerTask.cpp
  src/ServiceLocator.cpp
  src/State.cpp
  src/StateFactory.cpp
  src/resourcesystem/AnimationSetDefaultDefinitionFactory.cpp
  src/resourcesystem/AnimationSetResource.cpp
  src/resourcesystem/AnimationSetResourceDefinitionFactory.cpp
  src/resourcesystem/AudioBankResource.cpp
  src/resourcesystem/AudioBankDefaultDefinitionFactory.cpp
  src/resourcesystem/AudioBankResourceDefinitionFactory.cpp
  src/resourcesystem/DataStream.cpp
  src/resourcesystem/DirectoryResourceLocation.cpp
  src/resourcesystem/ImageDefaultDefinitionFactory.cpp
  src/resourcesystem/ImageResource.cpp
  src/resourcesystem/ImageResourceDefinitionFactory.cpp
  src/resourcesystem/ImageSetDefaultDefinitionFactory.cpp
  src/resourcesystem/ImageSetResource.cpp
  src/resourcesystem/ImageSetResourceDefinitionFactory.cpp
  src/resourcesystem/MaterialResource.cpp
  src/resourcesystem/MaterialResourceDefaultDefinitionFactory.cpp
  src/resourcesystem/MaterialResourceDefinitionFactory.cpp
  src/resourcesystem/ProgramDefaultDefinitionFactory.cpp
  src/resourcesystem/ProgramResource.cpp
  src/resourcesystem/ProgramResourceDefinitionFactory.cpp
  src/resourcesystem/Resource.cpp
  src/resourcesystem/ResourceDefinitionFactory.cpp
  src/resourcesystem/ResourceLocation.cpp
  src/resourcesystem/ResourceManager.cpp
  src/resourcesystem/ShaderDefaultDefinitionFactory.cpp
  src/resourcesystem/ShaderResource.cpp
  src/resourcesystem/ShaderResourceDefinitionFactory.cpp
  src/resourcesystem/TestFileDefaultDefinitionFactory.cpp
  src/resourcesystem/TextFileResource.cpp
  src/resourcesystem/TextFileResourceDefinitionFactory.cpp
  src/resourcesystem/XmlFileDefaultDefinitionFactory.cpp
  src/resourcesystem/XmlFileResource.cpp
  src/resourcesystem/XmlFileResourceDefinitionFactory.cpp)

# This directory is added directly by cpp/CMakeLists.txt, so the parent
# willpower/ directory's non-cache helper variable is not in scope here.
if (NOT DEFINED WILLPOWER_LIB_ARCH)
  set(WILLPOWER_LIB_ARCH x64)
endif()

if (WILLPOWER_BUILD_SHARED_LIBS)
  set(_willpower_application_lib_type SHARED)
else()
  set(_willpower_application_lib_type STATIC)
endif()

# The original project expects this external checkout at <repo>/ext/
# massivepolypusher. Override the cache entry when it lives elsewhere.
set(WILLPOWER_MPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../ext/massivepolypusher" CACHE PATH
  "Root of the MassivePolyPusher dependency")
set(WILLPOWER_APPLICATION_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party")

add_library(Willpower.Application ${_willpower_application_lib_type}
  ${WILLPOWER_APPLICATION_SOURCES} ${WILLPOWER_APPLICATION_HEADERS})
add_library(Willpower::Application ALIAS Willpower.Application)
target_compile_features(Willpower.Application PUBLIC cxx_std_20)
source_group("Header Files" FILES ${WILLPOWER_APPLICATION_HEADERS})

target_include_directories(Willpower.Application PUBLIC include)
target_include_directories(Willpower.Application PRIVATE
  "${WILLPOWER_MPP_DIR}/mpp/include"
  "${WILLPOWER_MPP_DIR}/mpp-mesh/include"
  "${WILLPOWER_MPP_DIR}/mpp-program/include"
  "${WILLPOWER_MPP_DIR}/vendor/include"
  "${WILLPOWER_MPP_DIR}/ext/utils/include"
  "${WILLPOWER_APPLICATION_THIRD_PARTY_DIR}/include"
  "${WILLPOWER_APPLICATION_THIRD_PARTY_DIR}/include/fmod/core")

target_link_libraries(Willpower.Application PUBLIC Willpower.Common)

# These are prebuilt SDK/import libraries, matching the vcxproj's Debug/Release
# names and directories. They are intentionally not required CMake targets.
target_link_directories(Willpower.Application PRIVATE
  "${WILLPOWER_MPP_DIR}/mpp/build/vs2026/lib/${WILLPOWER_LIB_ARCH}/$<CONFIG>"
  "${WILLPOWER_MPP_DIR}/mpp-mesh/build/vs2026/lib/${WILLPOWER_LIB_ARCH}/$<CONFIG>"
  "${WILLPOWER_MPP_DIR}/mpp-program/build/vs2026/lib/${WILLPOWER_LIB_ARCH}/$<CONFIG>"
  "${WILLPOWER_MPP_DIR}/ext/utils/build/vs2026/lib/${WILLPOWER_LIB_ARCH}/$<CONFIG>"
  "${WILLPOWER_APPLICATION_THIRD_PARTY_DIR}/lib/vs2026/${WILLPOWER_LIB_ARCH}/$<CONFIG>")
target_link_libraries(Willpower.Application PRIVATE
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,MppProgramd,MppProgram>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>
  FreeImage fmod_vc fmodstudio_vc fsbank_vc)

target_compile_definitions(Willpower.Application PRIVATE
  WP_USE_ASSERT_TRACE
  $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>
  $<IF:$<BOOL:${WILLPOWER_BUILD_SHARED_LIBS}>,WP_APPLICATION_DLL_EXPORT,WP_APPLICATION_STATIC_LIB>)
if (WIN32)
  target_compile_definitions(Willpower.Application PRIVATE WIN32 _WINDOWS)
endif()
set_target_properties(Willpower.Application PROPERTIES DEBUG_POSTFIX d)
