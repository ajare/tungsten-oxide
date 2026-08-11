# cpp/app — the native command-line host (NATIVE_GAME_RUNTIME_PLAN.md step 6),
# outside cpp/core per the plan's boundary: it owns track-file argument
# handling, the real-time frame loop, and console input, and links `core` for
# everything else (loading, baking, geometry, session, physics).
add_executable(track_runner main.cpp)
target_compile_features(track_runner PRIVATE cxx_std_20)
target_link_libraries(track_runner PRIVATE core)

# Multi-config generators leave Willpower DLLs in sibling output directories;
# put runtime dependencies beside the executable so a direct run works.
if (WIN32 AND (BUILD_SHARED_LIBS OR WILLPOWER_BUILD_SHARED_LIBS))
  add_custom_command(TARGET track_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:track_runner> $<TARGET_FILE_DIR:track_runner>
    COMMAND_EXPAND_LISTS)
endif()

# mesh_physics_diag (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 6.0) -- unlike track_runner above,
# this tool needs a real TrackCollisionSurface (mesh-mode physics), which means reading a .mppmodel
# via mpp::ModelSerializer. It reuses tungsten-monoxide's TrackCollisionBuild.cpp (the same BVH-
# construction code Map::load() uses) directly as an extra source file rather than linking the
# TungstenMonoxide DLL target itself, which is a Windows application DLL wired to the willpower
# resource system/AppLib and not meant to be a library dependency of anything else.
set(_mesh_physics_diag_legacy_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher")
set(_mesh_physics_diag_mpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massive-poly-pusher")
if (MESH_PHYSICS_DIAG_MPP_DIR STREQUAL _mesh_physics_diag_legacy_mpp_dir)
  set(MESH_PHYSICS_DIAG_MPP_DIR "${_mesh_physics_diag_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency (mesh_physics_diag)" FORCE)
else()
  set(MESH_PHYSICS_DIAG_MPP_DIR "${_mesh_physics_diag_mpp_dir}" CACHE PATH
    "Root of the MassivePolyPusher dependency (mesh_physics_diag)")
endif()
unset(_mesh_physics_diag_legacy_mpp_dir)
unset(_mesh_physics_diag_mpp_dir)
add_executable(mesh_physics_diag
  tools/mesh_physics_diag.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../tungsten-monoxide/src/TrackCollisionBuild.cpp")
target_compile_features(mesh_physics_diag PRIVATE cxx_std_20)
target_include_directories(mesh_physics_diag PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../tungsten-monoxide/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-helper/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-mesh/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-program/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/vendor/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/build/cmake/_deps/glew-2.3.1/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/ext/utils/include")
target_link_directories(mesh_physics_diag PRIVATE
  "${MESH_PHYSICS_DIAG_MPP_DIR}/build/cmake/lib/$<CONFIG>")
target_link_libraries(mesh_physics_diag PRIVATE
  core
  $<IF:$<CONFIG:Debug>,MassivePolyPusherd,MassivePolyPusher>
  $<IF:$<CONFIG:Debug>,MppHelperd,MppHelper>
  $<IF:$<CONFIG:Debug>,MppMeshd,MppMesh>
  $<IF:$<CONFIG:Debug>,Utilsd,Utils>)
if (WIN32)
  # mpp::RenderSystem's constructor calls glewInit(), which needs a real (if invisible) GL context
  # -- see mesh_physics_diag.cpp's createHeadlessGLContext() for why and how.
  target_link_libraries(mesh_physics_diag PRIVATE opengl32 gdi32 user32)
endif()

if (WIN32 AND (BUILD_SHARED_LIBS OR WILLPOWER_BUILD_SHARED_LIBS))
  add_custom_command(TARGET mesh_physics_diag POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:mesh_physics_diag> $<TARGET_FILE_DIR:mesh_physics_diag>
    COMMAND_EXPAND_LISTS)
endif()
# MppProgram and GLEW are transitive runtime dependencies that are not represented by CMake
# targets in this project, so copy the dependency's required DLLs explicitly.
add_custom_command(TARGET mesh_physics_diag POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${MESH_PHYSICS_DIAG_MPP_DIR}/build/cmake/bin/$<CONFIG>/$<IF:$<CONFIG:Debug>,MassivePolyPusherd.dll,MassivePolyPusher.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/build/cmake/bin/$<CONFIG>/$<IF:$<CONFIG:Debug>,MppMeshd.dll,MppMesh.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/build/cmake/bin/$<CONFIG>/$<IF:$<CONFIG:Debug>,MppHelperd.dll,MppHelper.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/build/cmake/bin/$<CONFIG>/$<IF:$<CONFIG:Debug>,MppProgramd.dll,MppProgram.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/build/cmake/bin/$<CONFIG>/$<IF:$<CONFIG:Debug>,Utilsd.dll,Utils.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/build/cmake/bin/$<CONFIG>/glew32.dll"
    $<TARGET_FILE_DIR:mesh_physics_diag>
  COMMAND_EXPAND_LISTS)

add_test(NAME mesh_physics_diag_bundled_track
  COMMAND mesh_physics_diag --validate
    "${CMAKE_CURRENT_SOURCE_DIR}/../tungsten-monoxide/resources/NewTrack.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tungsten-monoxide/resources/NewTrack.mppmodel"
    1)
