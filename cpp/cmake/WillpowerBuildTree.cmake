# Imports prebuilt Willpower libraries from the standalone ext/willpower build tree.
include_guard(GLOBAL)

function(tox_import_willpower willpower_source_dir)
  if (TARGET Willpower.Common)
    return()
  endif()

  set(TOX_WILLPOWER_BUILD_DIR "${willpower_source_dir}/build" CACHE PATH
    "Willpower standalone build tree")
  if (NOT EXISTS "${TOX_WILLPOWER_BUILD_DIR}/CMakeCache.txt")
    message(FATAL_ERROR
      "Willpower has not been configured at '${TOX_WILLPOWER_BUILD_DIR}'.\n"
      "Build the dependency first:\n"
      "  cmake -S ${willpower_source_dir} -B ${TOX_WILLPOWER_BUILD_DIR}\n"
      "  cmake --build ${TOX_WILLPOWER_BUILD_DIR} --config Release")
  endif()

  function(_tox_import_willpower_library target directory)
    add_library(${target} SHARED IMPORTED GLOBAL)
    set_target_properties(${target} PROPERTIES
      IMPORTED_CONFIGURATIONS "Debug;Release"
      IMPORTED_IMPLIB_DEBUG
        "${TOX_WILLPOWER_BUILD_DIR}/lib/Debug/${target}/${target}d.lib"
      IMPORTED_LOCATION_DEBUG
        "${TOX_WILLPOWER_BUILD_DIR}/bin/Debug/${target}/${target}d.dll"
      IMPORTED_IMPLIB_RELEASE
        "${TOX_WILLPOWER_BUILD_DIR}/lib/Release/${target}/${target}.lib"
      IMPORTED_LOCATION_RELEASE
        "${TOX_WILLPOWER_BUILD_DIR}/bin/Release/${target}/${target}.dll"
      INTERFACE_INCLUDE_DIRECTORIES "${willpower_source_dir}/${directory}/include")
  endfunction()

  _tox_import_willpower_library(Willpower.Common willpower.common)
  _tox_import_willpower_library(Willpower.Geometry willpower.geometry)
  _tox_import_willpower_library(Willpower.Application willpower.application)

  set_property(TARGET Willpower.Common APPEND PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES
      "${willpower_source_dir}/ext/earcut.hpp/include;${willpower_source_dir}/ext/SplineLibrary")
  set_property(TARGET Willpower.Geometry APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES Willpower.Common)
  set_property(TARGET Willpower.Application APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES Willpower.Common)
endfunction()
