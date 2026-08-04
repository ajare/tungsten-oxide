# cpp/model-xml — a small, focused static library owning exactly the <Model> XML fragment schema
# (docs/adr/0003-model-xml-layer.md, docs/TRACK_MODEL_LIST_PLAN.md Milestone 2): id/ModelFile/
# TrackData/per-mesh Type+Visible metadata, parsed/written via TinyXML2, either as a standalone
# document (bare <Model> root) or an already-located element embedded inside a Track resource's
# <Definition><Models> list. Deliberately no mpp/AssImp/SDL dependency — mirrors cpp/core's own
# "no heavyweight deps" posture. Shared by cpp/editor and cpp/model-tool, which both already link
# Willpower::Common (for its vendored TinyXML2) and share a build closure; NOT linked into
# cpp/tungsten-monoxide, which parses the same <Models> shape independently via wp::XmlNode
# (see the plan's architecture notes on why that split is intentional).
cmake_minimum_required(VERSION 3.20)
project(model_xml CXX)

if (NOT TARGET Willpower.Common)
  add_subdirectory(../willpower ${CMAKE_CURRENT_BINARY_DIR}/willpower)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
  add_compile_options(/W3 /EHsc)
endif()

add_library(model_xml STATIC
  include/ModelXml.hpp
  src/ModelXml.cpp)
target_include_directories(model_xml PUBLIC include)
target_link_libraries(model_xml PUBLIC Willpower::Common)

add_executable(model_xml_tests tests/model_xml_tests.cpp)
target_link_libraries(model_xml_tests PRIVATE model_xml)
add_test(NAME model_xml_tests COMMAND model_xml_tests)

# Willpower.Common is a shared library on multi-config generators; copy it beside the test exe so
# `ctest` can actually launch it, mirroring cpp/editor's track_editor post-build copy.
add_custom_command(TARGET model_xml_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:model_xml_tests> $<TARGET_FILE_DIR:model_xml_tests>
  COMMAND_EXPAND_LISTS)
