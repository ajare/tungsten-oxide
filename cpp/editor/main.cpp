// cpp/editor/main.cpp — track_editor: native ImGui/SDL2/OpenGL track editor
// (EDITOR_CPP_PORT_PLAN.md M0). This milestone only proves the toolchain: open
// a window, run one ImGui frame through the vendored docking build, and link
// `core` (unused so far) to confirm the combined cpp/ build order works.
// Editor logic (M1+) is not implemented yet.
#include <cstdio>

#include "imconfig.h"  // pulls in the vendored gl3w loader (see imconfig.h)
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

// M0 only opens a window; nothing from `core` is called yet (M1 wires in
// EditorTrackDefinition load/save/preview). `core` is still linked below to
// prove the combined cpp/ build order (track_editor -> core ->
// Willpower::Geometry) resolves.

int main(int, char**) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  const char* glslVersion = "#version 130";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  const SDL_WindowFlags windowFlags =
      static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Window* window =
      SDL_CreateWindow("track_editor (M0 skeleton)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
                        windowFlags);
  if (window == nullptr) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GLContext glContext = SDL_GL_CreateContext(window);
  if (glContext == nullptr) {
    std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(window, glContext);
  SDL_GL_SetSwapInterval(1);  // vsync

  if (gl3wInit() != 0) {
    std::fprintf(stderr, "gl3wInit failed\n");
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsDark();

  ImGui_ImplSDL2_InitForOpenGL(window, glContext);
  ImGui_ImplOpenGL3_Init(glslVersion);

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) running = false;
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(window)) {
        running = false;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("track_editor — M0 skeleton");
    ImGui::TextUnformatted("SDL2 + OpenGL3 + ImGui (docking) + gl3w link up.");
    ImGui::TextUnformatted("Editor logic lands in later EDITOR_CPP_PORT_PLAN.md milestones.");
    ImGui::End();

    ImGui::Render();
    int drawableWidth = 0, drawableHeight = 0;
    SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    glViewport(0, 0, drawableWidth, drawableHeight);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
