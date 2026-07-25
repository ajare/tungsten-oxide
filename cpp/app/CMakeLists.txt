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
