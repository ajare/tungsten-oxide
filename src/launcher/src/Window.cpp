#include <willpower/application/MouseButton.h>

#include "Window.h"

using namespace std;
using namespace wp::application;

Window::Window(string const& title, ProgramOptions const& options)
	: mWidth(options.screenWidth)
	, mHeight(options.screenHeight)
	, mVSync(options.vSync)
	, mFullscreen(options.fullScreen)
	, mTitle(title)
	, mDisplayDebugEnabled(options.debugging.inGame)
	, mDisplayDebugging(false)
{
	mCurKeyBuffer = new unsigned char[(int)Key::NUMKEYS];
	mPrevKeyBuffer = new unsigned char[(int)Key::NUMKEYS];

	for (int i = 0; i < (int)Key::NUMKEYS; ++i)
	{
		mCurKeyBuffer[i] = mPrevKeyBuffer[i] = 0;
	}
}

Window::~Window()
{
	delete[] mCurKeyBuffer;
	delete[] mPrevKeyBuffer;
}

int Window::getWidth() const
{
	return mWidth;
}

int Window::getHeight() const
{
	return mHeight;
}

void Window::recreate(int width, int height, bool fullScreen, bool vsync)
{
	mWidth = width;
	mHeight = height;
	mFullscreen = fullScreen;
	mVSync = vsync;

	destroy();
	create();
}

void Window::saveKeys()
{
	memcpy(mPrevKeyBuffer, mCurKeyBuffer, (int)Key::NUMKEYS * sizeof(unsigned char));
}

void Window::setKey(wp::application::Key key, bool down)
{
	mCurKeyBuffer[(int)key] = down ? 1 : 0;
}

void Window::setUserData(void* userData)
{
}

bool Window::keyPressed(wp::application::Key key) const
{
	return mCurKeyBuffer[(int)key] == 1 && mPrevKeyBuffer[(int)key] == 0;
}

bool Window::keyReleased(wp::application::Key key) const
{
	return mCurKeyBuffer[(int)key] == 0 && mPrevKeyBuffer[(int)key] == 1;
}

bool Window::keyDown(wp::application::Key key) const
{
	return mCurKeyBuffer[(int)key] == 1 && mPrevKeyBuffer[(int)key] == 1;
}

void Window::displayDebugging(bool display)
{
	mDisplayDebugging = display;
}

bool Window::displayDebugging() const
{
	return mDisplayDebugging;
}