#include <willpower/application/Key.h>
#include <willpower/application/MouseButton.h>

#include "sdl/WindowSDL.h"

#include "ExitApplicationException.h"

using namespace std;
using namespace wp;

extern Logger* gLogger;

WindowSDL::WindowSDL(string const& title, ProgramOptions const& options)
	: Window(title, options)
	, mWindow(nullptr)
{
	mKeyTranslator[SDLK_ESCAPE] = application::Key::Escape;
	mKeyTranslator[SDLK_1] = application::Key::_1;
	mKeyTranslator[SDLK_2] = application::Key::_2;
	mKeyTranslator[SDLK_3] = application::Key::_3;
	mKeyTranslator[SDLK_4] = application::Key::_4;
	mKeyTranslator[SDLK_5] = application::Key::_5;
	mKeyTranslator[SDLK_6] = application::Key::_6;
	mKeyTranslator[SDLK_7] = application::Key::_7;
	mKeyTranslator[SDLK_8] = application::Key::_8;
	mKeyTranslator[SDLK_9] = application::Key::_9;
	mKeyTranslator[SDLK_0] = application::Key::_0;
	mKeyTranslator[SDLK_MINUS] = application::Key::Minus;
	mKeyTranslator[SDLK_EQUALS] = application::Key::Equals;
	mKeyTranslator[SDLK_BACKSPACE] = application::Key::Backspace;
	mKeyTranslator[SDLK_TAB] = application::Key::Tab;
	mKeyTranslator[SDLK_A] = application::Key::A;
	mKeyTranslator[SDLK_B] = application::Key::B;
	mKeyTranslator[SDLK_C] = application::Key::C;
	mKeyTranslator[SDLK_D] = application::Key::D;
	mKeyTranslator[SDLK_E] = application::Key::E;
	mKeyTranslator[SDLK_F] = application::Key::F;
	mKeyTranslator[SDLK_G] = application::Key::G;
	mKeyTranslator[SDLK_H] = application::Key::H;
	mKeyTranslator[SDLK_I] = application::Key::I;
	mKeyTranslator[SDLK_J] = application::Key::J;
	mKeyTranslator[SDLK_K] = application::Key::K;
	mKeyTranslator[SDLK_L] = application::Key::L;
	mKeyTranslator[SDLK_M] = application::Key::M;
	mKeyTranslator[SDLK_N] = application::Key::N;
	mKeyTranslator[SDLK_O] = application::Key::O;
	mKeyTranslator[SDLK_P] = application::Key::P;
	mKeyTranslator[SDLK_Q] = application::Key::Q;
	mKeyTranslator[SDLK_R] = application::Key::R;
	mKeyTranslator[SDLK_S] = application::Key::S;
	mKeyTranslator[SDLK_T] = application::Key::T;
	mKeyTranslator[SDLK_U] = application::Key::U;
	mKeyTranslator[SDLK_V] = application::Key::V;
	mKeyTranslator[SDLK_W] = application::Key::W;
	mKeyTranslator[SDLK_X] = application::Key::X;
	mKeyTranslator[SDLK_Y] = application::Key::Y;
	mKeyTranslator[SDLK_Z] = application::Key::Z;

	mKeyTranslator[SDLK_LEFTBRACKET] = application::Key::LeftBracket;
	mKeyTranslator[SDLK_RIGHTBRACKET] = application::Key::RightBracket;
	mKeyTranslator[SDLK_RETURN] = application::Key::Enter;
	mKeyTranslator[SDLK_LCTRL] = application::Key::LeftControl;
	mKeyTranslator[SDLK_RCTRL] = application::Key::RightControl;
	mKeyTranslator[SDLK_SEMICOLON] = application::Key::Semicolon;
	mKeyTranslator[SDLK_AT] = application::Key::Apostrophe;
	mKeyTranslator[SDLK_GRAVE] = application::Key::Tilde;
	mKeyTranslator[SDLK_LSHIFT] = application::Key::LeftShift;
	mKeyTranslator[SDLK_RSHIFT] = application::Key::RightShift;
	mKeyTranslator[SDLK_BACKSLASH] = application::Key::Backslash;
	mKeyTranslator[SDLK_COMMA] = application::Key::Comma;
	mKeyTranslator[SDLK_PERIOD] = application::Key::Period;
	mKeyTranslator[SDLK_SLASH] = application::Key::Slash;
	mKeyTranslator[SDLK_HASH] = application::Key::Hash;
	mKeyTranslator[SDLK_LALT] = application::Key::LeftAlt;
	mKeyTranslator[SDLK_RALT] = application::Key::RightAlt;
	mKeyTranslator[SDLK_SPACE] = application::Key::Space;
	mKeyTranslator[SDLK_CAPSLOCK] = application::Key::CapsLock;
	mKeyTranslator[SDLK_NUMLOCKCLEAR] = application::Key::NumLock;
	mKeyTranslator[SDLK_SCROLLLOCK] = application::Key::ScrollLock;
	mKeyTranslator[SDLK_F1] = application::Key::F1;
	mKeyTranslator[SDLK_F2] = application::Key::F2;
	mKeyTranslator[SDLK_F3] = application::Key::F3;
	mKeyTranslator[SDLK_F4] = application::Key::F4;
	mKeyTranslator[SDLK_F5] = application::Key::F5;
	mKeyTranslator[SDLK_F6] = application::Key::F6;
	mKeyTranslator[SDLK_F7] = application::Key::F7;
	mKeyTranslator[SDLK_F8] = application::Key::F8;
	mKeyTranslator[SDLK_F9] = application::Key::F9;
	mKeyTranslator[SDLK_F10] = application::Key::F10;
	mKeyTranslator[SDLK_F11] = application::Key::F11;
	mKeyTranslator[SDLK_F12] = application::Key::F12;
	mKeyTranslator[SDLK_KP_MULTIPLY] = application::Key::NumpadMultiply;
	mKeyTranslator[SDLK_KP_MINUS] = application::Key::NumpadMinus;
	mKeyTranslator[SDLK_KP_PLUS] = application::Key::NumpadPlus;
	mKeyTranslator[SDLK_KP_DIVIDE] = application::Key::NumpadDivide;
	mKeyTranslator[SDLK_KP_0] = application::Key::Numpad_0;
	mKeyTranslator[SDLK_KP_1] = application::Key::Numpad_1;
	mKeyTranslator[SDLK_KP_2] = application::Key::Numpad_2;
	mKeyTranslator[SDLK_KP_3] = application::Key::Numpad_3;
	mKeyTranslator[SDLK_KP_4] = application::Key::Numpad_4;
	mKeyTranslator[SDLK_KP_5] = application::Key::Numpad_5;
	mKeyTranslator[SDLK_KP_6] = application::Key::Numpad_6;
	mKeyTranslator[SDLK_KP_7] = application::Key::Numpad_7;
	mKeyTranslator[SDLK_KP_8] = application::Key::Numpad_8;
	mKeyTranslator[SDLK_KP_9] = application::Key::Numpad_9;
	mKeyTranslator[SDLK_KP_PERIOD] = application::Key::NumpadPeriod;
	mKeyTranslator[SDLK_KP_ENTER] = application::Key::NumpadEnter;
	mKeyTranslator[SDLK_PRINTSCREEN] = application::Key::PrintScreen;
	mKeyTranslator[SDLK_PAUSE] = application::Key::Pause;
	mKeyTranslator[SDLK_HOME] = application::Key::Home;
	mKeyTranslator[SDLK_END] = application::Key::End;
	mKeyTranslator[SDLK_UP] = application::Key::UpArrow;
	mKeyTranslator[SDLK_DOWN] = application::Key::DownArrow;
	mKeyTranslator[SDLK_LEFT] = application::Key::LeftArrow;
	mKeyTranslator[SDLK_RIGHT] = application::Key::RightArrow;
	mKeyTranslator[SDLK_PAGEUP] = application::Key::PageUp;
	mKeyTranslator[SDLK_PAGEDOWN] = application::Key::PageDown;
	mKeyTranslator[SDLK_INSERT] = application::Key::Insert;
	mKeyTranslator[SDLK_DELETE] = application::Key::Delete;

	// Set up mouse
	mButtonTranslator = new application::MouseButton[application::MouseButton::NUMBUTTONS];

	mButtonTranslator[SDL_BUTTON_LEFT] = application::MouseButton::Left;
	mButtonTranslator[SDL_BUTTON_RIGHT] = application::MouseButton::Right;
	mButtonTranslator[SDL_BUTTON_MIDDLE] = application::MouseButton::Middle;
}

WindowSDL::~WindowSDL()
{
	delete[] mButtonTranslator;
}

void WindowSDL::create()
{
	// Use OpenGL 3.2

	if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3))
	{
		string err = SDL_GetError();
		throw exception(("Could not set OpenGL abbribute: " + err).c_str());
	}

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2))
	{
		string err = SDL_GetError();
		throw exception(("Could not set OpenGL abbribute: " + err).c_str());
	}

    if (!SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1))
	{
		string err = SDL_GetError();
		throw exception(("Could not set OpenGL abbribute: " + err).c_str());
	}

    if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24))
	{
		string err = SDL_GetError();
		throw exception(("Could not set OpenGL abbribute: " + err).c_str());
	}

	// Create window
	unsigned int windowFlags = SDL_WINDOW_OPENGL
		| SDL_WINDOW_MOUSE_FOCUS
		| SDL_WINDOW_MOUSE_CAPTURE;

	if (mFullscreen)
	{
		windowFlags |= SDL_WINDOW_FULLSCREEN;
	}
	
	mWindow = SDL_CreateWindow(mTitle.c_str(), mWidth, mHeight, windowFlags);
	
	if (!mWindow)
	{
		string err = SDL_GetError();
		throw exception(("Could not create SDL window: " + err).c_str());
	}

#ifdef _DEBUG
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

	mContextGL = SDL_GL_CreateContext(mWindow);
	if (!SDL_GL_MakeCurrent(mWindow, mContextGL))
	{
		throw exception("Could not set the OpenGL context.");
	}

	SDL_HideCursor();

	if (!SDL_SetWindowRelativeMouseMode(mWindow, true))
	{
		throw exception("Could not set relative mouse mode.");
	}

	if (!SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "1"))
	{
		throw exception("Could not set hint: SDL_HINT_MOUSE_RELATIVE_MODE_CENTER");
	}

	if (!SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE, "0.3"))
	{
		throw exception("Could not set hint: SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE");
	}
}

void WindowSDL::destroy()
{
	SDL_GL_DestroyContext(mContextGL);
	SDL_DestroyWindow(mWindow);
}
	
void WindowSDL::setFullscreen(bool fullscreen)
{
	if (fullscreen)
	{
		SDL_SetWindowFullscreen(mWindow, SDL_WINDOW_FULLSCREEN);
	}
	else
	{
		SDL_SetWindowFullscreen(mWindow, 0);
	}

	mFullscreen = fullscreen;
}

void WindowSDL::setSize(int width, int height)
{
	mWidth = width;
	mHeight = height;

	// if we're fullscreen, then go to windowed first, to stop other windows in background being resized.
	if (mFullscreen)
	{
		setFullscreen(false);
		SDL_SetWindowSize(mWindow, width, height);
		setFullscreen(true);
	}
	else
	{
		SDL_SetWindowSize(mWindow, width, height);
	}
}

void WindowSDL::show()
{
	SDL_GL_SwapWindow(mWindow);
}

application::KeyModifiers WindowSDL::getKeyModifiers(uint16_t mod)
{
	uint32_t km = (int)application::KeyModifiers::None;

	if (mod & SDL_KMOD_LSHIFT)
		km += (int)application::KeyModifiers::LeftShift;
	if (mod & SDL_KMOD_RSHIFT)
		km += (int)application::KeyModifiers::RightShift;
	if (mod & SDL_KMOD_SHIFT)
		km += (int)application::KeyModifiers::Shift;
	if (mod & SDL_KMOD_LCTRL)
		km += (int)application::KeyModifiers::LeftCtrl;
	if (mod & SDL_KMOD_RCTRL)
		km += (int)application::KeyModifiers::RightCtrl;
	if (mod & SDL_KMOD_CTRL)
		km += (int)application::KeyModifiers::Ctrl;
	if (mod & SDL_KMOD_LALT)
		km += (int)application::KeyModifiers::LeftAlt;
	if (mod & SDL_KMOD_RALT)
		km += (int)application::KeyModifiers::RightAlt;
	if (mod & SDL_KMOD_ALT)
		km += (int)application::KeyModifiers::Alt;
	if (mod & SDL_KMOD_NUM)
		km += (int)application::KeyModifiers::NumLock;
	if (mod & SDL_KMOD_CAPS)
		km += (int)application::KeyModifiers::CapsLock;

	return (application::KeyModifiers)km;
}

void WindowSDL::processEvents(StateManager* stateMgr)
{
	// Copy current keys to old
	saveKeys();

	SDL_Event evt;
	while (SDL_PollEvent(&evt))
	{
		//
		// Input events
		//
		float mouseX, mouseY;
		wp::application::Key key;
		switch (evt.type)
		{
		case SDL_EVENT_KEY_DOWN:
			key = mKeyTranslator[evt.key.key];
			setKey(key, true);

			if (keyPressed(key))
			{
				stateMgr->injectKeyInput(application::KeyEvent::Pressed, key, getKeyModifiers(evt.key.mod));
			}

			// Debugging
			if (mDisplayDebugEnabled && mKeyTranslator[evt.key.key] == application::Key::F11)
			{
				displayDebugging(!displayDebugging());
			}
			break;

		case SDL_EVENT_KEY_UP:
			key = mKeyTranslator[evt.key.key];
			setKey(key, false);

			if (keyReleased(key))
			{
				stateMgr->injectKeyInput(application::KeyEvent::Released, key, getKeyModifiers(evt.key.mod));
			}

			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			stateMgr->injectMouseButtonInput(application::MouseButtonEvent::Pressed, mButtonTranslator[evt.button.button], getKeyModifiers(evt.key.mod));
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			stateMgr->injectMouseButtonInput(application::MouseButtonEvent::Released, mButtonTranslator[evt.button.button], getKeyModifiers(evt.key.mod));
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			stateMgr->injectMouseWheelInput(evt.wheel.y);
			break;

		case SDL_EVENT_MOUSE_MOTION:
			SDL_GetMouseState(&mouseX, &mouseY);
			stateMgr->injectMouseMotionInput(mouseX, mouseY);
			break;

		case SDL_EVENT_QUIT:
			throw ExitApplicationException(0, "Application exited.");
		}
		
		//
		// Window events
		//
		switch (evt.window.type)
		{
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			throw ExitApplicationException(0, "Application exited.");
		}
	}

}
