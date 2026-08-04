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
set(MESH_PHYSICS_DIAG_MPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/massivepolypusher" CACHE PATH
  "Root of the MassivePolyPusher dependency (mesh_physics_diag)")
set(MESH_PHYSICS_DIAG_LIB_ARCH x64 CACHE STRING "Architecture used by the MassivePolyPusher SDK libraries (mesh_physics_diag)")

add_executable(mesh_physics_diag
  tools/mesh_physics_diag.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../tungsten-monoxide/src/TrackCollisionBuild.cpp")
target_compile_features(mesh_physics_diag PRIVATE cxx_std_20)
target_include_directories(mesh_physics_diag PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../tungsten-monoxide/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-helper/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-mesh/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-program/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/vendor/include"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/ext/utils/include")
target_link_directories(mesh_physics_diag PRIVATE
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp/build/vs2026/lib/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-helper/build/vs2026/lib/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-mesh/build/vs2026/lib/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>"
  "${MESH_PHYSICS_DIAG_MPP_DIR}/ext/utils/build/vs2026/lib/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>")
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
# mpp/Utils DLLs aren't discoverable via $<TARGET_RUNTIME_DLLS:...> (mesh_physics_diag links their
# prebuilt import libs directly, not a CMake target) -- copy them explicitly, mirroring
# TungstenMonoxide's own post-build step in cpp/tungsten-monoxide/CMakeLists.txt. MppProgram is a
# transitive runtime dependency of MppMesh/MassivePolyPusher (not directly linked here) but must
# still sit beside the exe for the Windows loader to resolve it.
add_custom_command(TARGET mesh_physics_diag POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp/build/vs2026/bin/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>/$<IF:$<CONFIG:Debug>,MassivePolyPusherd.dll,MassivePolyPusher.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-mesh/build/vs2026/bin/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>/$<IF:$<CONFIG:Debug>,MppMeshd.dll,MppMesh.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-helper/build/vs2026/bin/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>/$<IF:$<CONFIG:Debug>,MppHelperd.dll,MppHelper.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/mpp-program/build/vs2026/bin/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>/$<IF:$<CONFIG:Debug>,MppProgramd.dll,MppProgram.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/ext/utils/build/vs2026/bin/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>/$<IF:$<CONFIG:Debug>,Utilsd.dll,Utils.dll>"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/vendor/bin/vs2026/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>/glew32.dll"
    "${MESH_PHYSICS_DIAG_MPP_DIR}/vendor/bin/vs2026/${MESH_PHYSICS_DIAG_LIB_ARCH}/$<CONFIG>/FreeImage.dll"
    $<TARGET_FILE_DIR:mesh_physics_diag>
  COMMAND_EXPAND_LISTS)
