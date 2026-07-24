# Willpower.Common — spline, bounding-volume, acceleration-grid and
# batch-renderable-buffer primitives shared by the rest of Willpower.
#
# StringUtils is vendored in-tree (include/willpower/common/StringUtils.h,
# src/StringUtils.cpp), not pulled from an external library, so this library
# no longer links against anything from ext/: ext/Utils (Utils) and
# ext/MassivePolyPusher/mpp-mesh (MppMesh) were both dropped once their only
# consumers here (the old ext/Utils-based StringUtils, and
# QuadBatchRenderable) were gone. The TINYXML2_EXPORT define below may also be
# vestigial (no tinyxml2 usage appears in this library's sources).
set(WILLPOWER_COMMON_HEADERS
  include/willpower/common/AccelerationGrid.h
  include/willpower/common/BatchRenderable.h
  include/willpower/common/BezierSpline.h
  include/willpower/common/BoundingBox.h
  include/willpower/common/BoundingCircle.h
  include/willpower/common/BoundingConvexPolygon.h
  include/willpower/common/CachedDynamicAccelerationGrid.h
  include/willpower/common/CachedStaticAccelerationGrid.h
  include/willpower/common/CentripetalCatmullRomSpline.h
  include/willpower/common/CentripetalCatmullRomSplineLooping.h
  include/willpower/common/CubicBSpline.h
  include/willpower/common/CubicBSplineLooping.h
  include/willpower/common/DateTime.h
  include/willpower/common/DynamicAccelerationGrid.h
  include/willpower/common/Exceptions.h
  include/willpower/common/ExtendedAccelerationGrid.h
  include/willpower/common/ExtentsCalculator.h
  include/willpower/common/Globals.h
  include/willpower/common/IndexedTriangleBatchRenderable.h
  include/willpower/common/LineHit.h
  include/willpower/common/Logger.h
  include/willpower/common/MathsUtils.h
  include/willpower/common/Platform.h
  include/willpower/common/polypartition.h
  include/willpower/common/Renderable.h
  include/willpower/common/SplinePath.h
  include/willpower/common/StackWalker.h
  include/willpower/common/StringUtils.h
  include/willpower/common/Timer.h
  include/willpower/common/TriangleBatchRenderable.h
  include/willpower/common/Triangulation.h
  include/willpower/common/TriangleIntersection.h
  include/willpower/common/TriangleStripBatchRenderable.h
  include/willpower/common/Vector2.h
  include/willpower/common/WillpowerWalker.h)

set(WILLPOWER_COMMON_SOURCES
  src/AccelerationGrid.cpp
  src/BatchRenderable.cpp
  src/BezierSpline.cpp
  src/BoundingBox.cpp
  src/BoundingCircle.cpp
  src/BoundingConvexPolygon.cpp
  src/CachedDynamicAccelerationGrid.cpp
  src/CachedStaticAccelerationGrid.cpp
  src/CentripetalCatmullRomSpline.cpp
  src/CentripetalCatmullRomSplineLooping.cpp
  src/CubicBSpline.cpp
  src/CubicBSplineLooping.cpp
  src/DateTime.cpp
  src/ExtentsCalculator.cpp
  src/IndexedTriangleBatchRenderable.cpp
  src/LineHit.cpp
  src/Logger.cpp
  src/MathsUtils.cpp
  src/polypartition.cpp
  src/Renderable.cpp
  src/SplinePath.cpp
  src/StackWalker.cpp
  src/StringUtils.cpp
  src/DynamicAccelerationGrid.cpp
  src/Timer.cpp
  src/TriangleBatchRenderable.cpp
  src/Triangulation.cpp
  src/TriangleStripBatchRenderable.cpp
  src/Vector2.cpp
  src/WillpowerWalker.cpp)

add_library(Willpower.Common ${WILLPOWER_LIB_TYPE} ${WILLPOWER_COMMON_SOURCES} ${WILLPOWER_COMMON_HEADERS})
add_library(Willpower::Common ALIAS Willpower.Common)
source_group("Header Files" FILES ${WILLPOWER_COMMON_HEADERS})

target_include_directories(Willpower.Common PUBLIC include)
target_include_directories(Willpower.Common PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/include"  # spline_library, vendored under cpp/willpower/
  "${WILLPOWER_VENDOR_DIR}/include")

target_compile_definitions(Willpower.Common PRIVATE
  WP_USE_ASSERT_TRACE
  TINYXML2_EXPORT
  $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>
  $<IF:$<BOOL:${WILLPOWER_BUILD_SHARED_LIBS}>,WP_COMMON_DLL_EXPORT,WP_COMMON_STATIC_LIB>)
if (WIN32)
  target_compile_definitions(Willpower.Common PRIVATE WIN32 _WINDOWS
    $<$<BOOL:${WILLPOWER_BUILD_SHARED_LIBS}>:_USRDLL>)
endif()
set_target_properties(Willpower.Common PROPERTIES DEBUG_POSTFIX d)  # Debug build outputs Willpower.Commond.{dll,lib}
