# Vendored: stb_image

Single-header image decoder (`stb_image.h`), copied per the project convention
of embedding third-party sources directly (see `cpp/third_party/include/nlohmann`,
`cpp/editor/include/imgui`).

Source: https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
(public domain / MIT-0, per the license block at the end of the file).

Used by `TextureCache.cpp` (EDITOR_CPP_PORT_PLAN.md M7b) to decode texture
asset thumbnails for the tile-grid picker and upload them as GL textures.
`STB_IMAGE_IMPLEMENTATION` is defined in exactly that one translation unit.

Do not hand-edit; replace the whole file to update.
