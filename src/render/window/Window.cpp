#include <tchar.h>
#include "Window.h"

#include "common.h"

#include "global/Global.h"
#include "input/Input.h"
#include "input/MenuConfig.h"
#include "render/imgui/ImGui.h"
#include "render/imgui/game/xbox/CXboxContext.h"

#include "imgui.h"

#include "../D3d11/D3d11.h"

LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace AlphaRing::Render::Window {
    WNDPROC oldWndProc = nullptr;

    // A consumed key-down of a menu key (dashboard or debug) still reaches the game as WM_CHAR: TranslateMessage posted that character
    // before the key-down was dispatched here, possibly behind the key-up. It is taken out of the queue right away,
    // so a bind like SPACE or A-Z never types into the game, also after the dashboard's close animation; a chord that makes no character posted none, and nothing
    // later is touched.
    static void DropChar(HWND hWnd, WPARAM vk) {
        UINT scan = MapVirtualKey((UINT)vk, MAPVK_VK_TO_VSC);
        MSG msg; // all of the press's characters: a dead key before it makes two ('^' then the key's own)
        while (PeekMessage(&msg, hWnd, WM_CHAR, WM_CHAR, PM_NOREMOVE | PM_NOYIELD) && ((msg.lParam >> 16) & 0xFF) == scan)
            PeekMessage(&msg, hWnd, WM_CHAR, WM_CHAR, PM_REMOVE | PM_NOYIELD);
    }

    //todo: WM_IME_COMPOSITION Support
    static LRESULT dWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_DEVICECHANGE)
            AlphaRing::Input::RequestPadRescan();

        bool xboxOpen = g_pXboxContext && g_pXboxContext->isOpen();

        // Intercept keyboard/mouse before ImGui and the game while the Xbox
        // menu is open. The trigger key toggles the menu; everything else is
        // consumed here. Keyboard navigation is handled by GetAsyncKeyState
        // polling in Input::Update(), so no handleInput calls are needed here.
        if (xboxOpen) {
            if (uMsg == WM_KEYDOWN) {
                // Initial press only (lParam bit 30 = autorepeat): holding the
                // key would otherwise reopen the menu once it has closed.
                if (static_cast<int>(wParam) == g_menuConfig.keyboardVKey && !(lParam & (1 << 30)))
                    AlphaRing::Render::ImGui::ToggleXboxMenu();
                if (static_cast<int>(wParam) == g_menuConfig.keyboardVKey || static_cast<int>(wParam) == g_menuConfig.debugKeyboardVKey)
                    DropChar(hWnd, wParam);
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

        // Menu keys (only reached when the Xbox menu is closed): consume keydown
        // and keyup alike so a bind like SPACE or A-Z never reaches the game,
        // and open on the initial press only (lParam bit 30 = autorepeat). The
        // debug UI key is toggled by Input::Update(), so here it is only
        // consumed. While typing in a debug UI text field, both keys type.
        if ((uMsg == WM_KEYDOWN || uMsg == WM_KEYUP) &&
            !(AlphaRing::Global::Global()->show_imgui && ::ImGui::GetIO().WantTextInput)) {
            int vk = static_cast<int>(wParam);
            if (vk == g_menuConfig.keyboardVKey) {
                if (uMsg == WM_KEYDOWN && !(lParam & (1 << 30)))
                    AlphaRing::Render::ImGui::ToggleXboxMenu();
                if (uMsg == WM_KEYDOWN) DropChar(hWnd, wParam);
                return 0;
            }
            if (vk == g_menuConfig.debugKeyboardVKey) {
                if (uMsg == WM_KEYDOWN) DropChar(hWnd, wParam);
                return 0;
            }
        }

        // Swallow only the input the debug UI is using. Returning true for every
        // message while the cursor is over it also kept WM_PAINT, WM_SIZE,
        // WM_ACTIVATEAPP, WM_CLOSE etc. from the game, and WantCaptureMouse only
        // refreshes on the next ImGui frame, which a game waiting on one of
        // those never draws (seen as a hang under Proton).
        if (AlphaRing::Global::Global()->show_imgui) {
            auto& io = ::ImGui::GetIO();
            bool mouse_msg = (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST);
            bool key_msg = (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST);
            if ((mouse_msg && io.WantCaptureMouse) || (key_msg && io.WantCaptureKeyboard))
                return 0;
        }

        return CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
    }

    bool Initialize() {
        oldWndProc = (WNDPROC)SetWindowLongPtr(Graphics()->hwnd, GWLP_WNDPROC, (LONG_PTR)dWndProc);

        return oldWndProc != nullptr;
    }
}
