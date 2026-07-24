# Willpower.Geometry — mesh and polygon operations (offsetting, CSG, edge
# filtering, triangulation queries) built on top of Willpower.Common.
#
# MeshPropertyCollection.h #includes "willpower/serialization/SerializerChunk.h"
# and "willpower/serialization/SerializationUtils.h" — willpower.serialization
# isn't part of this checkout, so anything that pulls that header in won't
# compile until it (or at least its include/ headers) shows up. None of this
# library's own sources currently include it, so the library builds without
# it for now. The include path below points at where willpower.serialization
# would live as a sibling library; the root CMakeLists.txt also links the
# Willpower.Serialization target automatically once it exists.
set(WILLPOWER_GEOMETRY_HEADERS
  include/willpower/geometry/BezierOffsetter.h
  include/willpower/geometry/clipper.hpp
  include/willpower/geometry/ConvexOffsetter.h
  include/willpower/geometry/CsgUtils.h
  include/willpower/geometry/DirectedEdge.h
  include/willpower/geometry/DirectedEdgeLoop.h
  include/willpower/geometry/Edge.h
  include/willpower/geometry/EdgeFilter.h
  include/willpower/geometry/Exception.h
  include/willpower/geometry/Filter.h
  include/willpower/geometry/PolygonFilter.h
  include/willpower/geometry/VertexFilter.h
  include/willpower/geometry/Mesh.h
  include/willpower/geometry/MeshCallbacks.h
  include/willpower/geometry/MeshHelpers.h
  include/willpower/geometry/MeshOperationOptions.h
  include/willpower/geometry/MeshOperationResults.h
  include/willpower/geometry/MeshOperations.h
  include/willpower/geometry/MeshPropertyCollection.h
  include/willpower/geometry/MeshQuery.h
  include/willpower/geometry/MeshUtils.h
  include/willpower/geometry/MeshValidator.h
  include/willpower/geometry/Offsetter.h
  include/willpower/geometry/OperationStatus.h
  include/willpower/geometry/Platform.h
  include/willpower/geometry/Polygon.h
  include/willpower/geometry/TypeConverters.h
  include/willpower/geometry/Types.h
  include/willpower/geometry/UserAttributes.h
  include/willpower/geometry/Vertex.h)

set(WILLPOWER_GEOMETRY_SOURCES
  src/BezierOffsetter.cpp
  src/clipper.cpp
  src/ConvexOffsetter.cpp
  src/CsgUtils.cpp
  src/DirectedEdgeLoop.cpp
  src/Edge.cpp
  src/EdgeFilter.cpp
  src/Filter.cpp
  src/Mesh.cpp
  src/MeshHelpers.cpp
  src/MeshOperations.cpp
  src/MeshQuery.cpp
  src/MeshUtils.cpp
  src/MeshValidator.cpp
  src/Offsetter.cpp
  src/Polygon.cpp
  src/PolygonFilter.cpp
  src/TypeConverters.cpp
  src/Vertex.cpp
  src/VertexFilter.cpp)

add_library(Willpower.Geometry ${WILLPOWER_LIB_TYPE} ${WILLPOWER_GEOMETRY_SOURCES} ${WILLPOWER_GEOMETRY_HEADERS})
add_library(Willpower::Geometry ALIAS Willpower.Geometry)
source_group("Header Files" FILES ${WILLPOWER_GEOMETRY_HEADERS})

target_include_directories(Willpower.Geometry PUBLIC include)
target_include_directories(Willpower.Geometry PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../willpower.serialization/include"  # missing, see note above
  "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/include")             # spline_library, vendored under cpp/willpower/

target_link_libraries(Willpower.Geometry PUBLIC Willpower.Common)
if (TARGET Willpower.Serialization)
  target_link_libraries(Willpower.Geometry PUBLIC Willpower.Serialization)
endif()

target_compile_definitions(Willpower.Geometry PRIVATE
  WP_USE_ASSERT_TRACE
  $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>
  $<IF:$<BOOL:${WILLPOWER_BUILD_SHARED_LIBS}>,WP_GEOMETRY_DLL_EXPORT,WP_GEOMETRY_STATIC_LIB>)
if (WIN32)
  target_compile_definitions(Willpower.Geometry PRIVATE WIN32 _WINDOWS
    $<$<BOOL:${WILLPOWER_BUILD_SHARED_LIBS}>:_USRDLL>)
endif()
set_target_properties(Willpower.Geometry PROPERTIES DEBUG_POSTFIX d)  # Debug build outputs Willpower.Geometryd.{dll,lib}
