# cpp/model-io — the shared glTF/.mppmodel conversion layer (docs/GLTF_IMPORT_PLAN.md, M1).
#
# Two targets, split strictly on dependencies (docs/adr/0004-gltf-import.md, D2):
#
#   model_io_core   the vertex-layout contract, tangent/normal derivation, MeshSpecification-driven
#                   packing, embedded PbrMaterial construction and .mppmodel read/write. Links mpp
#                   but NOT AssImp, so cpp/tungsten-monoxide can consume it without dragging AssImp,
#                   Draco and zlib into the shipped game DLL's dependency closure.
#   model_io_gltf   AssImp loading, the manual node walk, and the two validations. Adds AssImp.
#
# Both link mpp's *build tree* directly, like every other mpp-consuming subproject here — see
# ../cmake/MppBuildTree.cmake.
#
# Build (as part of the combined cpp/ configure):
#   cmake -S cpp -B cpp/build
#   cmake --build cpp/build --config Release
cmake_minimum_required(VERSION 3.20)
project(model_io CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
  add_compile_options(/W3 /EHsc)
endif()

set(_model_io_repository_mpp_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massive-poly-pusher")
set(_model_io_cpp_willpower_mpp_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower/ext/massive-poly-pusher")
set(_model_io_mpp_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/willpower/ext/massive-poly-pusher")
if (MODEL_IO_MPP_DIR STREQUAL _model_io_repository_mpp_dir OR
    MODEL_IO_MPP_DIR STREQUAL _model_io_cpp_willpower_mpp_dir)
  set(MODEL_IO_MPP_DIR "${_model_io_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency" FORCE)
else()
  set(MODEL_IO_MPP_DIR "${_model_io_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency")
endif()
unset(_model_io_repository_mpp_dir)
unset(_model_io_cpp_willpower_mpp_dir)
unset(_model_io_mpp_dir)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/MppBuildTree.cmake")
tox_resolve_mpp_build_tree("${MODEL_IO_MPP_DIR}")

# --- model_io_core -----------------------------------------------------------------------------
add_library(model_io_core STATIC
  core/include/modelio/Diagnostics.hpp
  core/include/modelio/MeshLayout.hpp
  core/include/modelio/ModelData.hpp
  core/include/modelio/MppModelIo.hpp
  core/include/modelio/PbrMaterialBuild.hpp
  core/include/modelio/PbrMaterialRead.hpp
  core/include/modelio/PipelineMaterial.hpp
  core/include/modelio/Tangents.hpp
  core/include/modelio/VertexPacking.hpp
  core/src/Diagnostics.cpp
  core/src/MeshLayout.cpp
  core/src/MppModelIo.cpp
  core/src/PbrMaterialBuild.cpp
  core/src/PbrMaterialRead.cpp
  core/src/PipelineMaterial.cpp
  core/src/Tangents.cpp
  core/src/VertexPacking.cpp)
tox_add_mpp_supplemental_dependency(model_io_core)

target_compile_features(model_io_core PUBLIC cxx_std_20)
target_include_directories(model_io_core PUBLIC
  core/include
  "${MODEL_IO_MPP_DIR}/mpp/include"
  "${MODEL_IO_MPP_DIR}/mpp-data/include"
  "${MODEL_IO_MPP_DIR}/mpp-mesh/include"
  "${MODEL_IO_MPP_DIR}/mpp-resource-parsers/include"
  # mpp-program is not linked (it is an internal dependency of MassivePolyPusher.dll itself), but
  # mpp/UniformCollection.h -- a public header pulled in transitively -- includes
  # mpp/program/glslTypes.h, so its include directory is still required. Same reasoning as
  # cpp/model-tool and cpp/tungsten-monoxide.
  "${MODEL_IO_MPP_DIR}/mpp-program/include"
  "${MODEL_IO_MPP_DIR}/vendor/include"
  "${MODEL_IO_MPP_DIR}/ext/utils/include"
  # PbrMaterialBuild.cpp sets TextureParams' filter/wrap defaults with raw GL_LINEAR_MIPMAP_LINEAR/
  # GL_REPEAT constants, so this is a real dependency, not just a leftover from the days
  # mpp/ModelSerializer.h used to include <GL/glew.h> itself (MassivePolyPusher's "Decouple public
  # headers from implementation dependencies" migration removed that; every mpp public header this
  # target compiles against is GL-free now).
  "${TOX_MPP_GLEW_INCLUDE_DIR}")

target_link_directories(model_io_core PUBLIC "${TOX_MPP_BUILD_DIR}/lib/$<CONFIG>")
target_link_libraries(model_io_core PUBLIC
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  # mpp::PbrPipelineDocument's PbrPipelineResourceDocument::definition is an mpp::data::
  # StructuredData (constructing/copying/destroying one -- e.g. every mpp::PbrPipelineDocument
  # PipelineMaterial.cpp/GltfConvert.cpp/the editor/gltf_convert hold by value -- needs this
  # library's exported special members), not the ext/utils one, since MassivePolyPusher's own
  # "Decouple public headers from implementation dependencies" migration.
  $<IF:$<CONFIG:Debug>,MppDatad,MppData>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,MppResourceParsersd,MppResourceParsers>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>)

if (WIN32)
  # NOMINMAX only. mpp/Config.h used to define WIN32_LEAN_AND_MEAN itself (a C4005
  # macro-redefinition warning if this target defined it too); it no longer includes <Windows.h>
  # at all, but nothing here needs WIN32_LEAN_AND_MEAN's narrower <windows.h> either way.
  target_compile_definitions(model_io_core PUBLIC NOMINMAX)
endif()

# --- model_io_gltf -----------------------------------------------------------------------------
add_library(model_io_gltf STATIC
  gltf/include/modelio/GltfConvert.hpp
  gltf/include/modelio/AssetImport.hpp
  gltf/include/modelio/AssetExport.hpp
  gltf/src/GltfConvert.cpp
  gltf/src/AssetImport.cpp
  gltf/src/AssetExport.cpp)

target_compile_features(model_io_gltf PUBLIC cxx_std_20)
target_include_directories(model_io_gltf PUBLIC
  gltf/include
  # Assimp's generated config header lives in the build tree; its public headers in the source tree.
  "${TOX_MPP_BUILD_DIR}/ext/assimp/include"
  "${MODEL_IO_MPP_DIR}/ext/assimp/include")
target_link_libraries(model_io_gltf PUBLIC
  model_io_core
  $<IF:$<CONFIG:Debug>,assimp-vc145-mtd,assimp-vc145-mt>)

# --- Runtime deployment ------------------------------------------------------------------------
# mpp's libraries are linked by raw name through target_link_directories, not as CMake IMPORTED
# targets, so $<TARGET_RUNTIME_DLLS:...> cannot discover them. Mirrors cpp/model-tool's approach.
set(_model_io_copy_runtime_script [=[
if (NOT DEFINED OUTPUT_DIR OR NOT DEFINED CONFIG)
  message(FATAL_ERROR "OUTPUT_DIR and CONFIG are required")
endif()
set(_runtime_root "@TOX_MPP_BUILD_DIR@/bin/${CONFIG}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
if (EXISTS "${_runtime_root}")
  file(GLOB _runtime_files LIST_DIRECTORIES false "${_runtime_root}/*.dll")
  if (_runtime_files)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_runtime_files} "${OUTPUT_DIR}"
      COMMAND_ERROR_IS_FATAL ANY)
  endif()
endif()
]=])
file(CONFIGURE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/CopyModelIoRuntime.cmake"
  CONTENT "${_model_io_copy_runtime_script}"
  @ONLY)
unset(_model_io_copy_runtime_script)

# Published so any target that links model_io_* can deploy the same runtime set; the function is
# defined at global scope so sibling subprojects (cpp/gltf-convert, later cpp/editor) can call it.
set(MODEL_IO_RUNTIME_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/CopyModelIoRuntime.cmake"
  CACHE INTERNAL "Script that copies mpp/AssImp runtime DLLs beside a model-io consumer")

function(model_io_deploy_runtime target)
  if (WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        "-DOUTPUT_DIR=$<TARGET_FILE_DIR:${target}>"
        "-DCONFIG=$<CONFIG>"
        -P "${MODEL_IO_RUNTIME_SCRIPT}"
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()

# --- Tests -------------------------------------------------------------------------------------
enable_testing()
add_executable(model_io_tests tests/model_io_tests.cpp)
if (COMMAND tox_copy_asan_runtime)
  tox_copy_asan_runtime(model_io_tests)
endif()
target_link_libraries(model_io_tests PRIVATE model_io_gltf)
target_compile_definitions(model_io_tests PRIVATE
  "MODEL_IO_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../test-data/fixtures/gltf\"")
add_test(NAME model_io_tests COMMAND model_io_tests)
model_io_deploy_runtime(model_io_tests)
