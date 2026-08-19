# src/gltf-convert — gltf_convert, the headless glTF -> .mppmodel converter
# (docs/GLTF_IMPORT_PLAN.md, M1). This repository's analogue of MassivePolyPusher's ModelConvert,
# and the headless counterpart to the editor's "Import glTF..." action.
#
# All the work lives in src/model-io; this target is argument parsing and reporting only.
cmake_minimum_required(VERSION 3.20)
project(gltf_convert CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
  add_compile_options(/W3 /EHsc)
endif()

if (NOT TARGET model_io_gltf)
  add_subdirectory(../model-io ${CMAKE_CURRENT_BINARY_DIR}/model-io)
endif()

add_executable(gltf_convert main.cpp)
if (COMMAND tox_copy_asan_runtime)
  tox_copy_asan_runtime(gltf_convert)
endif()
target_compile_features(gltf_convert PRIVATE cxx_std_20)
target_link_libraries(gltf_convert PRIVATE model_io_gltf)

model_io_deploy_runtime(gltf_convert)
