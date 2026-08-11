// model_tool's imconfig.h -- copied from cpp/editor/include/imgui/imconfig.h with one change: this
// app must share a single GL loader with mpp::RenderSystem (GLEW, which mpp's own headers pull in
// unconditionally), whereas editor vendors its own gl3w loader specifically to avoid ever linking
// mpp at all -- see docs/adr/0001-model-tool.md, D2. Every default (commented-out) option below is
// unmodified stock Dear ImGui; only the trailing loader block differs from editor's copy.
#pragma once

//---- Define assertion handler. Defaults to calling assert().
//#define IM_ASSERT(_EXPR)  MyAssert(_EXPR)
//#define IM_ASSERT(_EXPR)  ((void)(_EXPR))     // Disable asserts

//---- Use 32-bit vertex indices (default is 16-bit) is one way to allow large meshes with more than 64K vertices.
//#define ImDrawIdx unsigned int

// model_tool: share mpp::RenderSystem's own GL loader (GLEW) rather than vendoring a second,
// independent loader (gl3w) the way cpp/editor does -- mpp's headers pull in GLEW unconditionally,
// and this is model-tool's own fresh process, so there is no duplicate-loader risk the way there
// would be trying to link both into the same binary (see MppModelExport.hpp's header comment on
// exactly that risk, which is why cpp/editor never links mpp at all). IMGUI_IMPL_OPENGL_LOADER_CUSTOM
// tells imgui_impl_opengl3.cpp to skip its own embedded minimal loader and assume GL function
// pointers are already available from whatever loader header the app provides here.
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GL/glew.h>
