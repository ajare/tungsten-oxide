# Vendored: gl3w

Pre-generated OpenGL core-profile loader (`GL/gl3w.h`, `GL/glcorearb.h`,
`src/gl3w/gl3w.c`), copied per the project convention of embedding third-party
sources directly (see `cpp/core/third_party/nlohmann`).

Source: the last commit of `ocornut/imgui` before its `examples/libs/gl3w`
directory was removed in favor of an embedded loader
(commit `7bbf8f2ab09976d674a56c49dfed22d850908c8a`, parent of
`459de65477423360176447e79df2f3a785b71f3d`, "Backends: OpenGL3: Embed our own
minimal GL loader based on gl3w..." — see
https://github.com/ocornut/imgui/issues/4445). Upstream gl3w
(https://github.com/skaslev/gl3w) ships only a Python generator, not
pre-built files, so this is the most recent known-good generated pair. No
`KHR/khrplatform.h` is required by this variant.

Do not hand-edit; regenerate by re-running gl3w's generator against a newer
`gl.xml` if these bindings ever need updating.
