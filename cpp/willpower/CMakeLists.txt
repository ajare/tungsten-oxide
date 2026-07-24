# Willpower — CMake build for the Willpower C++ library.
#
# Willpower is split into per-feature libraries under willpower.<name>/, each
# with public headers under include/willpower/<name>/ and implementation under
# src/. This checkout carries two of them: willpower.common (spline/bounding-
# volume/acceleration-grid math primitives) and willpower.geometry (mesh and
# polygon operations, built on top of Common). Other Willpower libraries may
# be dropped in later as sibling directories — each optional add_subdirectory
# below only fires if that directory actually exists, so adding one needs no
# edits here.
#
# Both present libraries also depend on code that isn't part of this checkout:
# willpower.common needs ext/Utils, and willpower.geometry additionally needs
# willpower.serialization (see each library's own CMakeLists.txt for exactly
# which headers). Neither will compile until those show up; point
# WILLPOWER_EXT_DIR / WILLPOWER_VENDOR_DIR at wherever the external
# dependencies end up living once you have them.
#
# Build:
#   cmake -S cpp/willpower -B cpp/willpower/build -G Ninja
#   cmake --build cpp/willpower/build
cmake_minimum_required(VERSION 3.20)
project(Willpower CXX)

set(CMAKE_CXX_STANDARD 20)         # Willpower's headers require C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (NOT WIN32)
  message(WARNING
    "Willpower was authored as a Windows/MSVC codebase (manual __declspec "
    "DLL exports, StackWalker.h, FMOD binaries) — configuring anyway, but "
    "expect it to need real portability work off Windows.")
endif()

if (MSVC)
  add_compile_options(/W4)
endif()

# Each Willpower library's Platform.h switches its API macro between
# __declspec(dllexport/dllimport) for a DLL build and empty for a static build,
# selected by a *_DLL_EXPORT vs. *_STATIC_LIB define, so both are genuinely
# supported. Flip this off to build static .lib files instead of .dll/.lib
# pairs.
option(WILLPOWER_BUILD_SHARED_LIBS "Build Willpower.* as DLLs; OFF builds static libraries" ON)
if (WILLPOWER_BUILD_SHARED_LIBS)
  set(WILLPOWER_LIB_TYPE SHARED)
else()
  set(WILLPOWER_LIB_TYPE STATIC)
endif()

# External dependencies live as siblings of willpower/ (i.e. cpp/ext,
# cpp/vendor); neither exists in this checkout, so point these at wherever you
# land them.
set(WILLPOWER_EXT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ext" CACHE PATH
  "Root of the ext/ dependencies (MassivePolyPusher, Utils)")
set(WILLPOWER_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor" CACHE PATH
  "Root of the vendor/ third-party libs (fmt, FreeImage, fmod, ...)")

# Willpower only targets x64.
set(WILLPOWER_LIB_ARCH "x64")

# The vendored external import libs use a trailing "d" on the Debug .lib and no
# suffix in Release (fmt.lib/fmtd.lib, Utils.lib/Utilsd.lib, MppMesh.lib/
# MppMeshd.lib, ...). FreeImage.lib and the FMOD libs use the SAME name in both
# configs, so don't route them through this helper.
function(willpower_link_debug_release target)
  foreach(lib ${ARGN})
    target_link_libraries(${target} PRIVATE $<IF:$<CONFIG:Debug>,${lib}d,${lib}>)
  endforeach()
endfunction()

add_subdirectory(willpower.common)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/willpower.serialization/CMakeLists.txt")
  add_subdirectory(willpower.serialization)
endif()

add_subdirectory(willpower.geometry)
