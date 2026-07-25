# Vendored: Dear ImGui (docking branch)

Copied per the project convention of embedding third-party sources directly
(see `cpp/core/third_party/nlohmann`) rather than a submodule/FetchContent,
because the brief calls for ImGui files living under this project's own
`include`/`src`.

Source: https://github.com/ocornut/imgui, branch `docking`,
commit `ca49eff3980443a97c470e09fe55b1740cfb9584`.

Files: core (`imgui.h/.cpp`, `imgui_internal.h`, `imgui_draw.cpp`,
`imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_demo.cpp`, `imconfig.h`,
`imstb_*.h`) plus the SDL2 and OpenGL3 backends
(`backends/imgui_impl_sdl2.*`, `backends/imgui_impl_opengl3.*`).

Do not hand-edit; re-copy from upstream `docking` to update. `imconfig.h` may
be locally edited if the editor needs custom ImGui config defines later —
check upstream diffs before overwriting it wholesale.
