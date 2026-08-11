# MppBuildTree.cmake — shared resolution of MassivePolyPusher's build tree.
#
# Every cpp/ subproject that links mpp consumes its *build tree* directly rather than an installed
# package: fetched headers under `_deps/`, import libraries under `lib/<CONFIG>/`, runtime DLLs
# under `bin/<CONFIG>/`, and assimp's generated headers under `ext/assimp/include`. Where that tree
# lands is the mpp builder's choice (`cmake -S . -B build/cmake` and `cmake -S . -B build` are both
# in use and produce identical internal layouts), so probe for it instead of hardcoding one
# spelling in six separate CMakeLists — a wrong guess otherwise surfaces much later as a
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
  set(_default "${mpp_source_dir}/build/cmake")
  set(_build_dir "")
  foreach(_candidate "${_default}" "${mpp_source_dir}/build")
    # CMakeCache.txt, not just the directory: `build/` exists as the parent of `build/cmake` in the
    # nested layout, so a plain EXISTS test would match it and resolve to an empty tree.
    if (EXISTS "${_candidate}/CMakeCache.txt")
      set(_build_dir "${_candidate}")
      break()
    endif()
  endforeach()

  if (_build_dir STREQUAL "")
    # Not fatal: cpp/core and its golden-fixture test targets (the documented
    # `ctest --test-dir cpp/build` suite) need nothing from mpp, and configuring for those alone is
    # legitimate. Only the mpp-linked targets below will fail, and this says why up front.
    set(_build_dir "${_default}")
    message(WARNING
      "MassivePolyPusher has no build tree under '${mpp_source_dir}/build/cmake' or "
      "'${mpp_source_dir}/build'. Targets that link mpp (Willpower.Application, applib, launcher, "
      "tungsten-monoxide, model_tool, mesh_physics_diag) will fail to compile against its headers "
      "and libraries. Configure and build MassivePolyPusher first, or point this project's "
      "*_MPP_DIR cache entry at a checkout that is already built.")
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
