#include <tchar.h>
#include "Window.h"

#include "common.h"

#include "global/Global.h"
#include "input/MenuConfig.h"
#include "render/imgui/game/xbox/CXboxContext.h"

#include "imgui.h"

#include "../D3d11/D3d11.h"

LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace AlphaRing::Render::Window {
    WNDPROC oldWndProc = nullptr;

    //todo: WM_IME_COMPOSITION Support
    static LRESULT dWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        bool xboxOpen = g_pXboxContext && g_pXboxContext->isOpen();

        // Intercept keyboard/mouse before ImGui and the game while the Xbox
        // menu is open. The trigger key toggles the menu; everything else is
        // consumed here. Keyboard navigation is handled by GetAsyncKeyState
        // polling in Input::Update(), so no handleInput calls are needed here.
        if (xboxOpen) {
            if (uMsg == WM_KEYDOWN) {
                if (static_cast<int>(wParam) == g_menuConfig.keyboardVKey) {
                    g_pXboxContext->close();
                }
                return 0; // consume all keyboard input
            }
            switch (uMsg) {
                case WM_KEYUP:
                case WM_CHAR:
                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                case WM_MOUSEMOVE:
                case WM_MOUSEWHEEL:
                    return 0;
            }
        }

        // Only feed ImGui real WndProc input while one of our menus is actually
        // visible — otherwise ImGui silently queues mouse/keyboard events for the
        // entire play session with nothing ever consuming them (NewFrame() is
        // also gated on menu visibility), which causes a glitch/jump once the
        // menu is finally opened again after a long session.
        bool menuActive = xboxOpen || AlphaRing::Global::Global()->show_imgui;
        if (menuActive && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
            return true;

        // Keyboard trigger to open the menu (only reached when menu is closed)
        if (uMsg == WM_KEYDOWN) {
            if (static_cast<int>(wParam) == g_menuConfig.keyboardVKey) {
                if (g_pXboxContext) g_pXboxContext->open();
                return 0;
            }
        }

        auto& io = ImGui::GetIO();

        if (io.WantCaptureMouse)
            if (AlphaRing::Global::Global()->show_imgui)
                return true;

        return CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
    }

    bool Initialize() {
        oldWndProc = (WNDPROC)SetWindowLongPtr(Graphics()->hwnd, GWLP_WNDPROC, (LONG_PTR)dWndProc);

        return oldWndProc != nullptr;
    }
}
