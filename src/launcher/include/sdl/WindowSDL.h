#pragma once

#include <SDL3/SDL.h>

#include "Window.h"

class WindowSDL : public Window {
  SDL_Window* mWindow;

  SDL_GLContext mContextGL;

  // Input translation
  std::map<int, wp::application::Key> mKeyTranslator;

  wp::application::MouseButton* mButtonTranslator;

private:
  wp::application::KeyModifiers getKeyModifiers(SDL_Keymod mod);

public:
  WindowSDL(std::string const& title, ProgramOptions const& options);

  ~WindowSDL();

  SDL_Window* getWindow() const;

  SDL_GLContext getContext() const;

  float getContentScale() const;

  void create();

  void destroy();

  void setFullscreen(bool fullscreen);

  void setSize(int width, int height);

  void show();

  void showCursor(bool show);

  void processEvents(StateManager* stateMgr);
};