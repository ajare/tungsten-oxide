# MppBuildTree.cmake — shared resolution of MassivePolyPusher's build tree.
#
# Every cpp/ subproject that links mpp consumes its *build tree* directly rather than an installed
# package: fetched headers under `_deps/`, import libraries under `lib/<CONFIG>/`, runtime DLLs
# under `bin/<CONFIG>/`, and assimp's generated headers under `ext/assimp/include`. Where that tree
# lands under Willpower's standalone build tree. Consumers may override TOX_MPP_BUILD_DIR, but all
# native targets share that one prebuilt tree — a wrong path otherwise surfaces much later as a
# "Cannot open include file: 'GL/glew.h'" from deep inside an mpp header, which names neither mpp
# nor the path that was actually looked for.
#
# Include with a source-relative path so each subproject keeps working when configured standalone
# (see their own header comments), not only through cpp/CMakeLists.txt.
include_guard(GLOBAL)

# Sets, in the caller's scope:
#   TOX_MPP_BUILD_DIR         the resolved build tree root
#   TOX_MPP_GLEW_INCLUDE_DIR  the fetched GLEW's include directory inside it
function(tox_resolve_mpp_build_tree mpp_source_dir)
  set(_default "${mpp_source_dir}/../../build/_deps/massive-poly-pusher-build")
  set(TOX_MPP_BUILD_DIR "${_default}" CACHE PATH
    "MassivePolyPusher build tree produced by the standalone Willpower build")
  set(_build_dir "${TOX_MPP_BUILD_DIR}")
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
  set(TOX_MPP_GLEW_INCLUDE_DIR "${_glew_include}" PARENT_SCOPE)
endfunction()
