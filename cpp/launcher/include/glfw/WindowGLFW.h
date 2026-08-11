#pragma once

#include <GL/glew.h>

#include <GLFW/glfw3.h>

#include "Window.h"
#include "StateManager.h"


class WindowGLFW : public Window
{
	GLFWwindow* mWindow;

	float mContentScale;

private:
	
	static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

	static void mousePosCallback(GLFWwindow* window, double x, double y);

	static void mouseEnterCallback(GLFWwindow* window, int entered);

	static void focusCallback(GLFWwindow* window, int focused);

	static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

	static void charCallback(GLFWwindow* window, unsigned int c);

public:

	WindowGLFW(std::string const& title, ProgramOptions const& options);

	~WindowGLFW();

	GLFWwindow* getWindow();

	float getContentScale() const;

	bool isActive() const;

	void create();

	void destroy();

	void setFullscreen(bool fullscreen);

	void setSize(int width, int height);

	void show();

	void showCursor(bool show);

	void setStateManager(StateManager* mgr);

	void processEvents(StateManager* stateMgr);
};