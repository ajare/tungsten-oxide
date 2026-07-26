#include "glfw/WindowGLFW.h"
#include "glfw/ImGuiGLFW.h"

#include <willpower/application/Key.h>
#include <willpower/application/MouseButton.h>

#include "StateManager.h"

using namespace std;
using namespace wp;

extern Logger* gLogger;

extern bool gDisplayDebugEnabled;

unsigned char gCurKeyBuffer[(int)application::Key::NUMKEYS];
unsigned char gPrevKeyBuffer[(int)application::Key::NUMKEYS];

map<int, wp::application::Key> gKeyTranslator = 
{
	{ GLFW_KEY_ESCAPE,			application::Key::Escape },
	{ GLFW_KEY_1,				application::Key::_1 },
	{ GLFW_KEY_2,				application::Key::_2 },
	{ GLFW_KEY_3,				application::Key::_3 },
	{ GLFW_KEY_4,				application::Key::_4 },
	{ GLFW_KEY_5,				application::Key::_5 },
	{ GLFW_KEY_6,				application::Key::_6 },
	{ GLFW_KEY_7,				application::Key::_7 },
	{ GLFW_KEY_8,				application::Key::_8 },
	{ GLFW_KEY_9,				application::Key::_9 },
	{ GLFW_KEY_0,				application::Key::_0 },
	{ GLFW_KEY_MINUS,			application::Key::Minus },
	{ GLFW_KEY_EQUAL,			application::Key::Equals },
	{ GLFW_KEY_BACKSPACE,		application::Key::Backspace },
	{ GLFW_KEY_TAB,				application::Key::Tab },
	{ GLFW_KEY_A,				application::Key::A },
	{ GLFW_KEY_B,				application::Key::B },
	{ GLFW_KEY_C,				application::Key::C },
	{ GLFW_KEY_D,				application::Key::D },
	{ GLFW_KEY_E,				application::Key::E },
	{ GLFW_KEY_F,				application::Key::F },
	{ GLFW_KEY_G,				application::Key::G },
	{ GLFW_KEY_H,				application::Key::H },
	{ GLFW_KEY_I,				application::Key::I },
	{ GLFW_KEY_J,				application::Key::J },
	{ GLFW_KEY_K,				application::Key::K },
	{ GLFW_KEY_L,				application::Key::L },
	{ GLFW_KEY_M,				application::Key::M },
	{ GLFW_KEY_N,				application::Key::N },
	{ GLFW_KEY_O,				application::Key::O },
	{ GLFW_KEY_P,				application::Key::P },
	{ GLFW_KEY_Q,				application::Key::Q },
	{ GLFW_KEY_R,				application::Key::R },
	{ GLFW_KEY_S,				application::Key::S },
	{ GLFW_KEY_T,				application::Key::T },
	{ GLFW_KEY_U,				application::Key::U },
	{ GLFW_KEY_V,				application::Key::V },
	{ GLFW_KEY_W,				application::Key::W },
	{ GLFW_KEY_X,				application::Key::X },
	{ GLFW_KEY_Y,				application::Key::Y },
	{ GLFW_KEY_Z,				application::Key::Z },
	{ GLFW_KEY_LEFT_BRACKET,	application::Key::LeftBracket },
	{ GLFW_KEY_RIGHT_BRACKET,	application::Key::RightBracket },
	{ GLFW_KEY_ENTER,			application::Key::Enter },
	{ GLFW_KEY_LEFT_CONTROL,	application::Key::LeftControl },
	{ GLFW_KEY_RIGHT_CONTROL,	application::Key::RightControl },
	{ GLFW_KEY_SEMICOLON,		application::Key::Semicolon },
	{ GLFW_KEY_APOSTROPHE,		application::Key::Apostrophe },
	{ GLFW_KEY_GRAVE_ACCENT,	application::Key::Tilde },
	{ GLFW_KEY_LEFT_SHIFT,		application::Key::LeftShift },
	{ GLFW_KEY_RIGHT_SHIFT,		application::Key::RightShift },
	{ GLFW_KEY_BACKSLASH,		application::Key::Backslash },
	{ GLFW_KEY_COMMA,			application::Key::Comma },
	{ GLFW_KEY_PERIOD,			application::Key::Period },
	{ GLFW_KEY_SLASH,			application::Key::Slash },
	//{ GLFW_KEY_GRAVE_ACCENT, application::Key::Hash },
	{ GLFW_KEY_LEFT_ALT,		application::Key::LeftAlt },
	{ GLFW_KEY_RIGHT_ALT,		application::Key::RightAlt },
	{ GLFW_KEY_SPACE,			application::Key::Space },
	{ GLFW_KEY_CAPS_LOCK,		application::Key::CapsLock },
	{ GLFW_KEY_NUM_LOCK,		application::Key::NumLock },
	{ GLFW_KEY_SCROLL_LOCK,		application::Key::ScrollLock },
	{ GLFW_KEY_F1,				application::Key::F1 },
	{ GLFW_KEY_F2,				application::Key::F2 },
	{ GLFW_KEY_F3,				application::Key::F3 },
	{ GLFW_KEY_F4,				application::Key::F4 },
	{ GLFW_KEY_F5,				application::Key::F5 },
	{ GLFW_KEY_F6,				application::Key::F6 },
	{ GLFW_KEY_F7,				application::Key::F7 },
	{ GLFW_KEY_F8,				application::Key::F8 },
	{ GLFW_KEY_F9,				application::Key::F9 },
	{ GLFW_KEY_F10,				application::Key::F10 },
	{ GLFW_KEY_F11,				application::Key::F11 },
	{ GLFW_KEY_F12,				application::Key::F12 },
	{ GLFW_KEY_KP_MULTIPLY,		application::Key::NumpadMultiply },
	{ GLFW_KEY_KP_SUBTRACT,		application::Key::NumpadMinus },
	{ GLFW_KEY_KP_ADD,			application::Key::NumpadPlus },
	{ GLFW_KEY_KP_DIVIDE,		application::Key::NumpadDivide },
	{ GLFW_KEY_KP_0,			application::Key::Numpad_0 },
	{ GLFW_KEY_KP_1,			application::Key::Numpad_1 },
	{ GLFW_KEY_KP_2,			application::Key::Numpad_2 },
	{ GLFW_KEY_KP_3,			application::Key::Numpad_3 },
	{ GLFW_KEY_KP_4,			application::Key::Numpad_4 },
	{ GLFW_KEY_KP_5,			application::Key::Numpad_5 },
	{ GLFW_KEY_KP_6,			application::Key::Numpad_6 },
	{ GLFW_KEY_KP_7,			application::Key::Numpad_7 },
	{ GLFW_KEY_KP_8,			application::Key::Numpad_8 },
	{ GLFW_KEY_KP_9,			application::Key::Numpad_9 },
	{ GLFW_KEY_KP_DECIMAL,		application::Key::NumpadPeriod },
	{ GLFW_KEY_KP_ENTER,		application::Key::NumpadEnter },
	{ GLFW_KEY_PRINT_SCREEN,	application::Key::PrintScreen },
	{ GLFW_KEY_PAUSE,			application::Key::Pause },
	{ GLFW_KEY_HOME,			application::Key::Home },
	{ GLFW_KEY_END,				application::Key::End },
	{ GLFW_KEY_UP,				application::Key::UpArrow },
	{ GLFW_KEY_DOWN,			application::Key::DownArrow },
	{ GLFW_KEY_LEFT,			application::Key::LeftArrow },
	{ GLFW_KEY_RIGHT,			application::Key::RightArrow },
	{ GLFW_KEY_PAGE_UP,			application::Key::PageUp },
	{ GLFW_KEY_PAGE_DOWN,		application::Key::PageDown },
	{ GLFW_KEY_INSERT,			application::Key::Insert },
	{ GLFW_KEY_DELETE,			application::Key::Delete }
};

application::MouseButton gButtonTranslator[(int)application::MouseButton::NUMBUTTONS];

application::KeyModifiers getKeyModifiers(int mod)
{
	uint32_t km = (int)application::KeyModifiers::None;

	if (mod & GLFW_MOD_SHIFT)
		km += (int)application::KeyModifiers::Shift;
	if (mod & GLFW_MOD_CONTROL)
		km += (int)application::KeyModifiers::Ctrl;
	if (mod & GLFW_MOD_ALT)
		km += (int)application::KeyModifiers::Alt;
	if (mod & GLFW_MOD_NUM_LOCK)
		km += (int)application::KeyModifiers::NumLock;
	if (mod & GLFW_MOD_CAPS_LOCK)
		km += (int)application::KeyModifiers::CapsLock;

	return (application::KeyModifiers)km;
}

void initialiseGlfwInput(GLFWwindow* window)
{
	for (int i = 0; i < (int)application::Key::NUMKEYS; ++i)
	{
		gCurKeyBuffer[i] = gPrevKeyBuffer[i] = 0;
	}

	gButtonTranslator[GLFW_MOUSE_BUTTON_LEFT] = application::MouseButton::Left;
	gButtonTranslator[GLFW_MOUSE_BUTTON_RIGHT] = application::MouseButton::Right;
	gButtonTranslator[GLFW_MOUSE_BUTTON_MIDDLE] = application::MouseButton::Middle;
}

void checkGlfwError()
{
	char const* errMsg;

	if (glfwGetError(&errMsg) != GLFW_NO_ERROR)
	{
		throw exception(("Could not create GLFW window: " + string(errMsg)).c_str());
	}
}

WindowGLFW::WindowGLFW(string const& title, ProgramOptions const& options)
	: Window(title, options)
	, mWindow(nullptr)
	, mContentScale(1.0f)
{
}

WindowGLFW::~WindowGLFW()
{
}

GLFWwindow* WindowGLFW::getWindow()
{
	return mWindow;
}

float WindowGLFW::getContentScale() const
{
	return mContentScale;
}

void WindowGLFW::create()
{
	gLogger->info(format("Creating window at {}x{}", mWidth, mHeight));

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	auto monitor = mFullscreen ? glfwGetPrimaryMonitor() : nullptr;
	
	mWindow = glfwCreateWindow(mWidth, mHeight, mTitle.c_str(), monitor, nullptr);
	checkGlfwError();

	glfwMakeContextCurrent(mWindow); checkGlfwError();

	// Set up input
	glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED); checkGlfwError();

	glfwSetCursorPos(mWindow, mWidth / 2, mHeight / 2); checkGlfwError();

	if (glfwRawMouseMotionSupported())
	{
		glfwSetInputMode(mWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE); checkGlfwError();
	}
	else
	{
		gLogger->info("Raw mouse motion is NOT supported");
	}

	glfwSetCursorEnterCallback(mWindow, WindowGLFW::mouseEnterCallback); checkGlfwError();
	glfwSetWindowFocusCallback(mWindow, WindowGLFW::focusCallback); checkGlfwError();
	glfwSetKeyCallback(mWindow, WindowGLFW::keyCallback); checkGlfwError();
	glfwSetMouseButtonCallback(mWindow, WindowGLFW::mouseButtonCallback); checkGlfwError();
	glfwSetCursorPosCallback(mWindow, WindowGLFW::mousePosCallback); checkGlfwError();
	glfwSetScrollCallback(mWindow, WindowGLFW::scrollCallback); checkGlfwError();
	glfwSetCharCallback(mWindow, WindowGLFW::charCallback); checkGlfwError(); 

	// Get content scale for 2d rendering
	float xScale, yScale;

	glfwGetWindowContentScale(mWindow, &xScale, &yScale); checkGlfwError();

	mContentScale = min(xScale, yScale);
	gLogger->info(format("Setting content scale to {}", mContentScale));

	// VSync
	gLogger->info(format("Setting Vsync to {}", mVSync));
	glfwSwapInterval(mVSync ? 1 : 0); checkGlfwError();

	initialiseGlfwInput(mWindow);
}

void WindowGLFW::destroy()
{
	gLogger->info("Destroying window");

	glfwDestroyWindow(mWindow);
	mWindow = nullptr;
}

void WindowGLFW::setFullscreen(bool fullscreen)
{
	throw exception("WindowGLFW::setFullscreen() Not implemented");
}

void WindowGLFW::setSize(int width, int height)
{
	throw exception("WindowGLFW::setSize() Not implemented");
}

void WindowGLFW::show()
{
	glfwSwapBuffers(mWindow);
}

void WindowGLFW::showCursor(bool show)
{
	glfwSetInputMode(mWindow, GLFW_CURSOR, show ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void WindowGLFW::setStateManager(StateManager* mgr)
{
	glfwSetWindowUserPointer(mWindow, mgr);
}

void WindowGLFW::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	auto stateMgr = (StateManager*)glfwGetWindowUserPointer(window);

	if (!stateMgr)
	{
		return;
	}

	auto modifiers = getKeyModifiers(mods);
	wp::application::Key appKey = gKeyTranslator[key];

	switch (action)
	{
	case GLFW_PRESS:
		gCurKeyBuffer[(int)appKey] = 1;

		if (gCurKeyBuffer[(int)appKey] && !gPrevKeyBuffer[(int)appKey])
		{
			stateMgr->injectKeyInput(application::KeyEvent::Pressed, appKey, modifiers);
		}

		if (key == GLFW_KEY_F1)
		{
			gDisplayDebugEnabled = !gDisplayDebugEnabled;
		}
		break;

	case GLFW_RELEASE:
		gCurKeyBuffer[(int)appKey] = 0;

		if (!gCurKeyBuffer[(int)appKey] && gPrevKeyBuffer[(int)appKey])
		{
			stateMgr->injectKeyInput(application::KeyEvent::Released, appKey, modifiers);
		}
		break;
	}

	if (stateMgr->imGuiActive())
	{
		imguiKeyCallback(window, key, scancode, action, mods);
	}
}

void WindowGLFW::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	auto stateMgr = (StateManager*)glfwGetWindowUserPointer(window);

	if (!stateMgr)
	{
		return;
	}

	auto modifiers = getKeyModifiers(mods);
	wp::application::MouseButton appButton = gButtonTranslator[button];

	if (action == GLFW_PRESS)
	{
		stateMgr->injectMouseButtonInput(application::MouseButtonEvent::Pressed, appButton, modifiers);
	}
	else if (action == GLFW_RELEASE)
	{
		stateMgr->injectMouseButtonInput(application::MouseButtonEvent::Released, appButton, modifiers);
	}

	if (stateMgr->imGuiActive())
	{
		imGuiMouseButtonCallback(window, button, action, mods);
	}
}

void WindowGLFW::mousePosCallback(GLFWwindow* window, double x, double y)
{
	auto stateMgr = (StateManager*)glfwGetWindowUserPointer(window);

	if (!stateMgr)
	{
		return;
	}

	stateMgr->injectMouseMotionInput((float)x, (float)y);

	if (stateMgr->imGuiActive())
	{
		imGuiCursorPosCallback(window, x, y);
	}
}

void WindowGLFW::mouseEnterCallback(GLFWwindow* window, int entered)
{
	auto stateMgr = (StateManager*)glfwGetWindowUserPointer(window);

	if (!stateMgr)
	{
		return;
	}

	if (stateMgr->imGuiActive())
	{
		imguiCursorEnterCallback(window, entered);
	}
}

void WindowGLFW::focusCallback(GLFWwindow* window, int focused)
{
	auto stateMgr = (StateManager*)glfwGetWindowUserPointer(window);

	if (!stateMgr)
	{
		return;
	}

	if (stateMgr->imGuiActive())
	{
		imguiWindowFocusCallback(window, focused);
	}
}

void WindowGLFW::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	auto stateMgr = (StateManager*)glfwGetWindowUserPointer(window);

	if (!stateMgr)
	{
		return;
	}

	if (stateMgr->imGuiActive())
	{
		imguiScrollCallback(window, xoffset, yoffset);
	}
}

void WindowGLFW::charCallback(GLFWwindow* window, unsigned int c)
{
	auto stateMgr = (StateManager*)glfwGetWindowUserPointer(window);

	if (!stateMgr)
	{
		return;
	}

	if (stateMgr->imGuiActive())
	{
		imguiCharCallback(window, c);
	}
}

void WindowGLFW::processEvents(StateManager* stateMgr)
{
	memcpy(gPrevKeyBuffer, gCurKeyBuffer, (int)application::Key::NUMKEYS * sizeof(unsigned char));

	glfwPollEvents();
}

bool WindowGLFW::isActive() const
{
	return !glfwWindowShouldClose(mWindow);
}