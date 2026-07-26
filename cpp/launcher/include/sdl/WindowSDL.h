#pragma once

#include <sdl3/SDL.h>

#include "Window.h"

class WindowSDL : public Window
{
	SDL_Window* mWindow;

	SDL_GLContext mContextGL;

	// Input translation
	std::map<int, wp::application::Key> mKeyTranslator;

	wp::application::MouseButton* mButtonTranslator;

private:

	wp::application::KeyModifiers getKeyModifiers(uint16_t mod);

public:

	WindowSDL(std::string const& title, ProgramOptions const& options);

	~WindowSDL();

	void create();

	void destroy();

	void setFullscreen(bool fullscreen);

	void setSize(int width, int height);
	
	void show();

	void processEvents(StateManager* stateMgr);
};