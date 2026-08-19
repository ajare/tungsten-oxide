#pragma once

#include <willpower/application/Key.h>

#include "ProgramOptions.h"
#include "StateManager.h"

class Window
{
	bool mDisplayDebugging;

	unsigned char* mCurKeyBuffer, *mPrevKeyBuffer;

protected:

	int mWidth, mHeight;

	bool mVSync;

	bool mFullscreen;

	std::string mTitle;

	bool mDisplayDebugEnabled;

private:

	int translateKeyCode(int rawKey);

	int translateMouseCode(int rawButton);

protected:

	void saveKeys();

	void setKey(wp::application::Key key, bool down);

public:

	Window(std::string const& title, ProgramOptions const& options);

	virtual ~Window();

	int getWidth() const;

	int getHeight() const;

	void recreate(int width, int height, bool fullScreen, bool vsync);

	virtual void create() = 0;

	virtual void destroy() = 0;

	virtual void setFullscreen(bool fullscreen) = 0;

	virtual void setSize(int width, int height) = 0;

	virtual void show() = 0;

	virtual void processEvents(StateManager* stateMgr) = 0;

	virtual void setUserData(void* userData);

	bool keyPressed(wp::application::Key key) const;

	bool keyReleased(wp::application::Key key) const;

	bool keyDown(wp::application::Key key) const;

	void displayDebugging(bool display);

	bool displayDebugging() const;
};