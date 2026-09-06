# MppBuildTree.cmake — shared resolution of MassivePolyPusher's build tree.
#
# Every src/ subproject that links mpp consumes its standalone build directly rather than an
# installed package. MPP is configured below Willpower's build tree, which contains fetched and
# generated headers, but deliberately writes libraries and DLLs below its own source tree. Keep
# those locations separate, as BooleanWorld does: conflating them lets compilation succeed but
# eventually fails at link time because the generated build tree has no lib/<CONFIG> directory.
# Consumers may override either location, but all native targets share the same pair.
#
# Include with a source-relative path so each subproject keeps working when configured standalone
# (see their own header comments), not only through src/CMakeLists.txt.
include_guard(GLOBAL)

# Sets, in the caller's scope:
#   TOX_MPP_BUILD_DIR         the resolved CMake binary tree root
#   TOX_MPP_OUTPUT_DIR        the resolved library and runtime output root
#   TOX_MPP_GLEW_INCLUDE_DIR  the fetched GLEW's include directory inside the binary tree
function(tox_resolve_mpp_build_tree mpp_source_dir)
  set(_default_build_dir "${mpp_source_dir}/../../build/_deps/massive-poly-pusher-build")
  set(_default_output_dir "${mpp_source_dir}/build")
  set(TOX_MPP_BUILD_DIR "${_default_build_dir}" CACHE PATH
    "MassivePolyPusher CMake binary tree produced by the standalone Willpower build")
  set(TOX_MPP_OUTPUT_DIR "${_default_output_dir}" CACHE PATH
    "MassivePolyPusher library and runtime output tree")
  set(_build_dir "${TOX_MPP_BUILD_DIR}")
  set(_output_dir "${TOX_MPP_OUTPUT_DIR}")
  if (NOT EXISTS "${_build_dir}/CMakeCache.txt")
    message(FATAL_ERROR
      "MassivePolyPusher has not been configured at '${_build_dir}'. Build Willpower first, or "
      "set TOX_MPP_BUILD_DIR to its prebuilt MassivePolyPusher tree.")
  endif()

  # GLEW arrives through FetchContent, so its directory carries the fetched version number. Glob
  # rather than pinning one, so bumping mpp's GLEW doesn't silently drop the include directory.
  file(GLOB _glew_includes "${_build_dir}/_deps/glew-*/include")
  if (_glew_includes)
    list(GET _glew_includes 0 _glew_include)
  else()
    # Nothing fetched (yet) -- fall back to the version mpp currently pins, so the path is at least
    # the one that will exist once mpp is built.
    set(_glew_include "${_build_dir}/_deps/glew-2.3.1/include")
  endif()

  set(TOX_MPP_BUILD_DIR "${_build_dir}" PARENT_SCOPE)
  set(TOX_MPP_OUTPUT_DIR "${_output_dir}" PARENT_SCOPE)
  set(TOX_MPP_GLEW_INCLUDE_DIR "${_glew_include}" PARENT_SCOPE)
endfunction()

# Willpower's external-project build produces only the MPP libraries Willpower itself consumes.
# The combined root build also needs the resource parsers, app support, and Assimp. Make those
# supplemental libraries an actual build-graph dependency instead of requiring a one-time manual
# build before Visual Studio can link the first native target.
function(tox_add_mpp_supplemental_dependency target)
  if (NOT TARGET tox_mpp_supplemental_dependencies)
    add_custom_target(tox_mpp_supplemental_dependencies
      COMMAND "${CMAKE_COMMAND}" --build "${TOX_MPP_BUILD_DIR}"
        --config "$<CONFIG>" --parallel
        --target MppResourceParsers MppAppSupport assimp
      COMMENT "Building supplemental MassivePolyPusher dependencies"
      VERBATIM)
    set_target_properties(tox_mpp_supplemental_dependencies PROPERTIES FOLDER Dependencies)
  endif()

  add_dependencies(${target} tox_mpp_supplemental_dependencies)
endfunction()
