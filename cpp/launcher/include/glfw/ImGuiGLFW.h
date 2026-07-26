#pragma once

#include "imgui/imgui.h"

#include "WindowGLFW.h"


struct ImGuiGlfwData
{
    GLFWwindow* window;
    double time;
    GLFWwindow* mouseWindow;
    GLFWcursor* mouseCursors[ImGuiMouseCursor_COUNT];
    ImVec2 lastValidMousePos;
    bool callbacksChainForAllWindows;

#ifdef _WIN32
    WNDPROC                 prevWndProc;
#endif

    ImGuiGlfwData()
    {
        memset((void*)this, 0, sizeof(*this));
    }
};

void initialiseImGuiForGlfw(GLFWwindow* window);

void shutdownImGuiForGlfw();

void imguiKeyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods);

void imGuiMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

void imGuiCursorPosCallback(GLFWwindow* window, double x, double y);

void imguiCursorEnterCallback(GLFWwindow* window, int entered);

void imguiWindowFocusCallback(GLFWwindow* window, int focused);

void imguiScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

void imguiCharCallback(GLFWwindow* window, unsigned int c);

void imGuiNewFrame();
