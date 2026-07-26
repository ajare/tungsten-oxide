#include "GLFW/ImGuiGLFW.h"

#ifdef _WIN32
  #undef APIENTRY
  #ifndef GLFW_EXPOSE_NATIVE_WIN32
    #define GLFW_EXPOSE_NATIVE_WIN32
  #endif
  #include <GLFW/glfw3native.h>   // for glfwGetWin32Window()
#endif

#define GLFW_VERSION_COMBINED           (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)

#ifdef GLFW_RESIZE_NESW_CURSOR
  #define GLFW_HAS_NEW_CURSORS          (GLFW_VERSION_COMBINED >= 3400)
#else
  #define GLFW_HAS_NEW_CURSORS          (0)
#endif

#define GLFW_HAS_GAMEPAD_API            (GLFW_VERSION_COMBINED >= 3300)
#define GLFW_HAS_GETKEYNAME             (GLFW_VERSION_COMBINED >= 3200)
#define GLFW_HAS_GETERROR               (GLFW_VERSION_COMBINED >= 3300)


static ImGuiGlfwData* getBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGuiGlfwData*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

static ImGuiKey glfwKeyToImGuiKey(int keycode, int scancode)
{
    IM_UNUSED(scancode);
    switch (keycode)
    {
    case GLFW_KEY_TAB: return ImGuiKey_Tab;
    case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
    case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
    case GLFW_KEY_UP: return ImGuiKey_UpArrow;
    case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
    case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
    case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
    case GLFW_KEY_HOME: return ImGuiKey_Home;
    case GLFW_KEY_END: return ImGuiKey_End;
    case GLFW_KEY_INSERT: return ImGuiKey_Insert;
    case GLFW_KEY_DELETE: return ImGuiKey_Delete;
    case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
    case GLFW_KEY_SPACE: return ImGuiKey_Space;
    case GLFW_KEY_ENTER: return ImGuiKey_Enter;
    case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
    case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
    case GLFW_KEY_COMMA: return ImGuiKey_Comma;
    case GLFW_KEY_MINUS: return ImGuiKey_Minus;
    case GLFW_KEY_PERIOD: return ImGuiKey_Period;
    case GLFW_KEY_SLASH: return ImGuiKey_Slash;
    case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
    case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
    case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
    case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
    case GLFW_KEY_WORLD_1: return ImGuiKey_Oem102;
    case GLFW_KEY_WORLD_2: return ImGuiKey_Oem102;
    case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
    case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
    case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
    case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
    case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
    case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
    case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
    case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
    case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
    case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
    case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
    case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
    case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
    case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
    case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
    case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
    case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
    case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
    case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
    case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
    case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
    case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
    case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
    case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
    case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
    case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
    case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
    case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
    case GLFW_KEY_MENU: return ImGuiKey_Menu;
    case GLFW_KEY_0: return ImGuiKey_0;
    case GLFW_KEY_1: return ImGuiKey_1;
    case GLFW_KEY_2: return ImGuiKey_2;
    case GLFW_KEY_3: return ImGuiKey_3;
    case GLFW_KEY_4: return ImGuiKey_4;
    case GLFW_KEY_5: return ImGuiKey_5;
    case GLFW_KEY_6: return ImGuiKey_6;
    case GLFW_KEY_7: return ImGuiKey_7;
    case GLFW_KEY_8: return ImGuiKey_8;
    case GLFW_KEY_9: return ImGuiKey_9;
    case GLFW_KEY_A: return ImGuiKey_A;
    case GLFW_KEY_B: return ImGuiKey_B;
    case GLFW_KEY_C: return ImGuiKey_C;
    case GLFW_KEY_D: return ImGuiKey_D;
    case GLFW_KEY_E: return ImGuiKey_E;
    case GLFW_KEY_F: return ImGuiKey_F;
    case GLFW_KEY_G: return ImGuiKey_G;
    case GLFW_KEY_H: return ImGuiKey_H;
    case GLFW_KEY_I: return ImGuiKey_I;
    case GLFW_KEY_J: return ImGuiKey_J;
    case GLFW_KEY_K: return ImGuiKey_K;
    case GLFW_KEY_L: return ImGuiKey_L;
    case GLFW_KEY_M: return ImGuiKey_M;
    case GLFW_KEY_N: return ImGuiKey_N;
    case GLFW_KEY_O: return ImGuiKey_O;
    case GLFW_KEY_P: return ImGuiKey_P;
    case GLFW_KEY_Q: return ImGuiKey_Q;
    case GLFW_KEY_R: return ImGuiKey_R;
    case GLFW_KEY_S: return ImGuiKey_S;
    case GLFW_KEY_T: return ImGuiKey_T;
    case GLFW_KEY_U: return ImGuiKey_U;
    case GLFW_KEY_V: return ImGuiKey_V;
    case GLFW_KEY_W: return ImGuiKey_W;
    case GLFW_KEY_X: return ImGuiKey_X;
    case GLFW_KEY_Y: return ImGuiKey_Y;
    case GLFW_KEY_Z: return ImGuiKey_Z;
    case GLFW_KEY_F1: return ImGuiKey_F1;
    case GLFW_KEY_F2: return ImGuiKey_F2;
    case GLFW_KEY_F3: return ImGuiKey_F3;
    case GLFW_KEY_F4: return ImGuiKey_F4;
    case GLFW_KEY_F5: return ImGuiKey_F5;
    case GLFW_KEY_F6: return ImGuiKey_F6;
    case GLFW_KEY_F7: return ImGuiKey_F7;
    case GLFW_KEY_F8: return ImGuiKey_F8;
    case GLFW_KEY_F9: return ImGuiKey_F9;
    case GLFW_KEY_F10: return ImGuiKey_F10;
    case GLFW_KEY_F11: return ImGuiKey_F11;
    case GLFW_KEY_F12: return ImGuiKey_F12;
    case GLFW_KEY_F13: return ImGuiKey_F13;
    case GLFW_KEY_F14: return ImGuiKey_F14;
    case GLFW_KEY_F15: return ImGuiKey_F15;
    case GLFW_KEY_F16: return ImGuiKey_F16;
    case GLFW_KEY_F17: return ImGuiKey_F17;
    case GLFW_KEY_F18: return ImGuiKey_F18;
    case GLFW_KEY_F19: return ImGuiKey_F19;
    case GLFW_KEY_F20: return ImGuiKey_F20;
    case GLFW_KEY_F21: return ImGuiKey_F21;
    case GLFW_KEY_F22: return ImGuiKey_F22;
    case GLFW_KEY_F23: return ImGuiKey_F23;
    case GLFW_KEY_F24: return ImGuiKey_F24;
    default: return ImGuiKey_None;
    }
}

static void updateKeyModifiers(GLFWwindow* window)
{
    ImGuiIO& io = ImGui::GetIO();

    io.AddKeyEvent(ImGuiMod_Ctrl, (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));
    io.AddKeyEvent(ImGuiMod_Shift, (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS));
    io.AddKeyEvent(ImGuiMod_Alt, (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS));
    io.AddKeyEvent(ImGuiMod_Super, (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS));
}

static int translateUntranslatedKey(int key, int scancode)
{
#if GLFW_HAS_GETKEYNAME
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
    {
        return key;
    }

    GLFWerrorfun prevErrorCallback = glfwSetErrorCallback(nullptr);
    const char* key_name = glfwGetKeyName(key, scancode);
    glfwSetErrorCallback(prevErrorCallback);

#if GLFW_HAS_GETERROR // Eat errors
    (void)glfwGetError(nullptr);
#endif

    if (key_name && key_name[0] != 0 && key_name[1] == 0)
    {
        const char char_names[] = "`-=[]\\,;\'./";
        const int char_keys[] = { GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
        IM_ASSERT(IM_ARRAYSIZE(char_names) == IM_ARRAYSIZE(char_keys));
        if (key_name[0] >= '0' && key_name[0] <= '9') { key = GLFW_KEY_0 + (key_name[0] - '0'); }
        else if (key_name[0] >= 'A' && key_name[0] <= 'Z') { key = GLFW_KEY_A + (key_name[0] - 'A'); }
        else if (key_name[0] >= 'a' && key_name[0] <= 'z') { key = GLFW_KEY_A + (key_name[0] - 'a'); }
        else if (const char* p = strchr(char_names, key_name[0])) { key = char_keys[p - char_names]; }
    }
    // if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
#else
    IM_UNUSED(scancode);
#endif
    return key;
}

static ImGuiMouseSource getMouseSourceFromMessageExtraInfo()
{
    LPARAM extra_info = ::GetMessageExtraInfo();
    if ((extra_info & 0xFFFFFF80) == 0xFF515700)
    {
        return ImGuiMouseSource_Pen;
    }
    if ((extra_info & 0xFFFFFF80) == 0xFF515780)
    {
        return ImGuiMouseSource_TouchScreen;
    }

    return ImGuiMouseSource_Mouse;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto bd = getBackendData();
    switch (msg)
    {
    case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
        ImGui::GetIO().AddMouseSourceEvent(getMouseSourceFromMessageExtraInfo());
        break;
    }
    return ::CallWindowProcW(bd->prevWndProc, hWnd, msg, wParam, lParam);
}

void initialiseImGuiForGlfw(GLFWwindow* window)
{
    ImGuiIO& io = ImGui::GetIO();

    auto bd = new ImGuiGlfwData();

    io.BackendPlatformUserData = (void*)bd;
    io.BackendPlatformName = "GLFW3";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    bd->window = window;
    bd->time = 0.0;

    ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
#if GLFW_VERSION_COMBINED < 3300
    platformIO.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { glfwSetClipboardString(getBackendData()->window, text); };
    platformIO.Platform_GetClipboardTextFn = [](ImGuiContext*) { return glfwGetClipboardString(getBackendData()->window); };
#else
    platformIO.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { glfwSetClipboardString(nullptr, text); };
    platformIO.Platform_GetClipboardTextFn = [](ImGuiContext*) { return glfwGetClipboardString(nullptr); };
#endif

    GLFWerrorfun prevErrorCallback = glfwSetErrorCallback(nullptr);
    bd->mouseCursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    bd->mouseCursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    bd->mouseCursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    bd->mouseCursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    bd->mouseCursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
#if GLFW_HAS_NEW_CURSORS
    bd->mouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
    bd->mouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
    bd->mouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
    bd->mouseCursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
#else
    bd->MouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    bd->MouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    bd->MouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    bd->MouseCursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
#endif
    glfwSetErrorCallback(prevErrorCallback);
#if GLFW_HAS_GETERROR && !defined(__EMSCRIPTEN__) // Eat errors (see #5908)
    (void)glfwGetError(nullptr);
#endif

    // Set platform dependent data in viewport
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    mainViewport->PlatformHandle = (void*)bd->window;
#ifdef _WIN32
    mainViewport->PlatformHandleRaw = glfwGetWin32Window(bd->window);
#elif defined(__APPLE__)
    mainViewport->PlatformHandleRaw = (void*)glfwGetCocoaWindow(bd->Window);
#else
    IM_UNUSED(mainViewport);
#endif

    // Windows: register a WndProc hook so we can intercept some messages.
#ifdef _WIN32
    bd->prevWndProc = (WNDPROC)::GetWindowLongPtrW((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC);
    IM_ASSERT(bd->prevWndProc != nullptr);
    ::SetWindowLongPtrW((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)WndProc);
#endif
}

void shutdownImGuiForGlfw()
{
    auto bd = getBackendData();
    
    // Clear this here so that no random callbacks get unexpectedly fired before we're
    // cleaned up.
    glfwSetWindowUserPointer(bd->window, nullptr);

    ImGuiIO& io = ImGui::GetIO();

    for (ImGuiMouseCursor cursor_n = 0; cursor_n < ImGuiMouseCursor_COUNT; cursor_n++)
    {
        glfwDestroyCursor(bd->mouseCursors[cursor_n]);
    }

    // Windows: restore our WndProc hook
#ifdef _WIN32
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ::SetWindowLongPtrW((HWND)main_viewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)bd->prevWndProc);
    bd->prevWndProc = nullptr;
#endif

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad);

    delete bd;
}


static void updateMouseData()
{
    auto bd = getBackendData();
    ImGuiIO& io = ImGui::GetIO();

    GLFWwindow* window = bd->window;
    const bool isWindowFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
     
    if (isWindowFocused)
    {
        // (Optional) Set OS mouse position from Dear ImGui if requested (rarely used, only when io.ConfigNavMoveSetMousePos is enabled by user)
        if (io.WantSetMousePos)
        {
            glfwSetCursorPos(window, (double)io.MousePos.x, (double)io.MousePos.y);
        }

        // (Optional) Fallback to provide mouse position when focused (ImGui_ImplGlfw_CursorPosCallback already provides this when hovered or captured)
        if (bd->mouseWindow == nullptr)
        {
            double mouse_x, mouse_y;
            glfwGetCursorPos(window, &mouse_x, &mouse_y);
            bd->lastValidMousePos = ImVec2((float)mouse_x, (float)mouse_y);
            io.AddMousePosEvent((float)mouse_x, (float)mouse_y);
        }
    }
}

static void updateMouseCursor()
{
    ImGuiIO& io = ImGui::GetIO();
    auto bd = getBackendData();

    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(bd->window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
    {
        return;
    }

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    GLFWwindow* window = bd->window;

    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
    else
    {
        // Show OS mouse cursor
        glfwSetCursor(window, bd->mouseCursors[imgui_cursor] ? bd->mouseCursors[imgui_cursor] : bd->mouseCursors[ImGuiMouseCursor_Arrow]);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void imguiKeyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods)
{
    auto bd = getBackendData();

    if (action != GLFW_PRESS && action != GLFW_RELEASE)
    {
        return;
    }

    updateKeyModifiers(window);

    keycode = translateUntranslatedKey(keycode, scancode);

    ImGuiIO& io = ImGui::GetIO();
    ImGuiKey imguiKey = glfwKeyToImGuiKey(keycode, scancode);

    io.AddKeyEvent(imguiKey, (action == GLFW_PRESS));
    io.SetKeyEventNativeData(imguiKey, keycode, scancode); // To support legacy indexing (<1.87 user code)
}

void imGuiMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    auto bd = getBackendData();

    updateKeyModifiers(window);

    ImGuiIO& io = ImGui::GetIO();
    if (button >= 0 && button < ImGuiMouseButton_COUNT)
    {
        io.AddMouseButtonEvent(button, action == GLFW_PRESS);
    }
}

void imGuiCursorPosCallback(GLFWwindow* window, double x, double y)
{
    auto bd = getBackendData();

    ImGuiIO& io = ImGui::GetIO();

    io.AddMousePosEvent((float)x, (float)y);
    bd->lastValidMousePos = ImVec2((float)x, (float)y);
}

void imguiCursorEnterCallback(GLFWwindow* window, int entered)
{
    auto bd = getBackendData();

    ImGuiIO& io = ImGui::GetIO();
    if (entered)
    {
        bd->mouseWindow = window;
        io.AddMousePosEvent(bd->lastValidMousePos.x, bd->lastValidMousePos.y);
    }
    else if (!entered && bd->mouseWindow == window)
    {
        bd->lastValidMousePos = io.MousePos;
        bd->mouseWindow = nullptr;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
}

void imguiWindowFocusCallback(GLFWwindow* window, int focused)
{
    auto bd = getBackendData();

    ImGuiIO& io = ImGui::GetIO();
    io.AddFocusEvent(focused != 0);
}

void imguiScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto bd = getBackendData();

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent((float)xoffset, (float)yoffset);
}

void imguiCharCallback(GLFWwindow* window, unsigned int c)
{
    auto bd = getBackendData();

    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharacter(c);
}

void imGuiNewFrame()
{
    ImGuiIO& io = ImGui::GetIO();

    auto bd = getBackendData();

    // Setup display size (every frame to accommodate for window resizing)
    int w, h;
    int displayWidth, displayHeight;
    glfwGetWindowSize(bd->window, &w, &h);
    glfwGetFramebufferSize(bd->window, &displayWidth, &displayHeight);
    io.DisplaySize = ImVec2((float)w, (float)h);
    if (w > 0 && h > 0)
    {
        io.DisplayFramebufferScale = ImVec2((float)displayWidth / (float)w, (float)displayHeight / (float)h);
    }

    // Setup time step
    double currentTime = glfwGetTime();
    if (currentTime <= bd->time)
    {
        currentTime = bd->time + 0.00001f;
    }

    io.DeltaTime = bd->time > 0.0 ? (float)(currentTime - bd->time) : (float)(1.0f / 60.0f);
    bd->time = currentTime;

    updateMouseData();
    updateMouseCursor();
}