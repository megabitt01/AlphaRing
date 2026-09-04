#include "Input.h"
#include "MenuConfig.h"

#include "common.h"
#include "MinHook.h"

#include "imgui.h"
#include "global/Global.h"
#include "render/imgui/game/xbox/CXboxContext.h"

static HMODULE hModule;
static DWORD (WINAPI* g_pXInputGetState)(_In_ DWORD dwUserIndex, _Out_ XINPUT_STATE* pState) WIN_NOEXCEPT;
static DWORD (WINAPI* g_pXInputSetState)(_In_ DWORD dwUserIndex, _In_ XINPUT_VIBRATION* pVibration) WIN_NOEXCEPT;

static DWORD WINAPI XInputGetStateDetour(DWORD dwUserIndex, XINPUT_STATE* pState) {
    DWORD result = g_pXInputGetState(dwUserIndex, pState);
    if (result == ERROR_SUCCESS && pState && g_pXboxContext && g_pXboxContext->isOpen())
        ZeroMemory(&pState->Gamepad, sizeof(pState->Gamepad));
    return result;
}

namespace AlphaRing::Input {
    bool Init() {
        if ((hModule = GetModuleHandleA("XINPUT1_3.dll")) ||
            (hModule = GetModuleHandleA("XINPUT1_4.dll")) ||
            (hModule = GetModuleHandleA("XINPUT9_1_0.dll"))) {
            g_pXInputGetState = (decltype(g_pXInputGetState))GetProcAddress(hModule, "XInputGetState");
            g_pXInputSetState = (decltype(g_pXInputSetState))GetProcAddress(hModule, "XInputSetState");
        }

        assertm(hModule != nullptr, "failed to find xinput module");

        auto xInputAddr = (LPVOID)GetProcAddress(hModule, "XInputGetState");
        MH_STATUS mhStatus = MH_Initialize();
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_INITIALIZED) {
            if (MH_CreateHook(xInputAddr, &XInputGetStateDetour,
                              reinterpret_cast<LPVOID*>(&g_pXInputGetState)) == MH_OK) {
                MH_EnableHook(xInputAddr);
            }
        }

        g_menuConfig = MenuConfig::load();

        return true;
    }

    bool Shutdown() {
        return true;
    }

    bool GetXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
        if (!g_pXInputGetState || !pState) return false;
        memset(pState, 0, sizeof(XINPUT_STATE));
        return g_pXInputGetState(dwUserIndex, pState) == ERROR_SUCCESS;
    }

    void SetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) {
        if (!g_pXInputSetState) return;
        g_pXInputSetState(dwUserIndex, pVibration);
    }

    bool Update() {
        static bool b_pressed    = false;
        static WORD prevButtons  = 0;
        static bool stickActive[4] = {};  // up, down, left, right

        // keyboard nav state — tracks rising edge for each key
        struct NavKey { int vk; InputCommand cmd; bool wasDown; };
        static NavKey navKeys[] = {
            {'W',       InputCommand::Up,     false},
            {VK_UP,     InputCommand::Up,     false},
            {'S',       InputCommand::Down,   false},
            {VK_DOWN,   InputCommand::Down,   false},
            {'A',       InputCommand::Left,   false},
            {VK_LEFT,   InputCommand::Left,   false},
            {'D',       InputCommand::Right,  false},
            {VK_RIGHT,  InputCommand::Right,  false},
            {VK_RETURN, InputCommand::Select, false},
            {VK_ESCAPE, InputCommand::Back,   false},
        };

        // Zero-initialized so a disconnected/missing controller (GetXInputGetState
        // failure) still leaves us with a neutral state instead of bailing out —
        // keyboard nav below must keep working even with no gamepad plugged in.
        XINPUT_STATE state{};
        GetXInputGetState(0, &state);

        WORD buttons     = state.Gamepad.wButtons;
        WORD justPressed = buttons & ~prevButtons;
        prevButtons      = buttons;

        // configurable debug UI keyboard key
        static bool debugKeyWasDown = false;
        int debugKey = g_menuConfig.debugKeyboardVKey;
        bool debugKeyIsDown = (GetAsyncKeyState(debugKey) & 0x8000) != 0;
        if (debugKeyIsDown && !debugKeyWasDown) {
            AlphaRing::Global::Global()->show_imgui = !AlphaRing::Global::Global()->show_imgui;
        }
        debugKeyWasDown = debugKeyIsDown;
        
        // configurable debug UI combo
        WORD debugCombo = g_menuConfig.debugComboMask;
        if ((buttons & debugCombo) == debugCombo && (justPressed & debugCombo) != 0) {
            AlphaRing::Global::Global()->show_imgui = !AlphaRing::Global::Global()->show_imgui;
            return false;
        }

        // configurable xbox menu combo
        WORD menuCombo = g_menuConfig.controllerComboMask;
        if ((buttons & menuCombo) == menuCombo && (justPressed & menuCombo) != 0) {
            if (g_pXboxContext) {
                if (g_pXboxContext->isOpen()) g_pXboxContext->close();
                else g_pXboxContext->open();
            }
        }

        // navigation and input consumption while Xbox menu is open
        if (g_pXboxContext && g_pXboxContext->isOpen()) {
            // d-pad / face button navigation
            const auto send = [&](WORD btn, InputCommand cmd) {
                if (justPressed & btn) g_pXboxContext->handleInput(cmd);
            };
            send(XINPUT_GAMEPAD_DPAD_UP,    InputCommand::Up);
            send(XINPUT_GAMEPAD_DPAD_DOWN,  InputCommand::Down);
            send(XINPUT_GAMEPAD_DPAD_LEFT,  InputCommand::Left);
            send(XINPUT_GAMEPAD_DPAD_RIGHT, InputCommand::Right);
            send(XINPUT_GAMEPAD_A,          InputCommand::Select);
            send(XINPUT_GAMEPAD_B,          InputCommand::Back);

            // left stick navigation — edge-triggered on deadzone crossing
            const SHORT dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
            bool upNow    = state.Gamepad.sThumbLY >  dz;
            bool downNow  = state.Gamepad.sThumbLY < -dz;
            bool leftNow  = state.Gamepad.sThumbLX < -dz;
            bool rightNow = state.Gamepad.sThumbLX >  dz;

            if (upNow    && !stickActive[0]) g_pXboxContext->handleInput(InputCommand::Up);
            if (downNow  && !stickActive[1]) g_pXboxContext->handleInput(InputCommand::Down);
            if (leftNow  && !stickActive[2]) g_pXboxContext->handleInput(InputCommand::Left);
            if (rightNow && !stickActive[3]) g_pXboxContext->handleInput(InputCommand::Right);

            stickActive[0] = upNow;
            stickActive[1] = downNow;
            stickActive[2] = leftNow;
            stickActive[3] = rightNow;

            // keyboard navigation via polling
            for (auto& k : navKeys) {
                bool isDown = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
                if (isDown && !k.wasDown) g_pXboxContext->handleInput(k.cmd);
                k.wasDown = isDown;
            }

            return true; // consume all remaining controller input
        }

        // reset edge-tracking state when menu is closed
        stickActive[0] = stickActive[1] = stickActive[2] = stickActive[3] = false;
        for (auto& k : navKeys) k.wasDown = false;

        // thumbstick mouse control for debug UI
        if (AlphaRing::Global::Global()->show_imgui) {
            const auto f_speed = [](SHORT x, SHORT y) -> ImVec2 {
                const auto speed = 5.0f;
                const auto f_normalize = [](SHORT sThumb) -> float {
                    const auto deadZone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
                    return (abs(sThumb) > deadZone) ? (sThumb / 32767.0f) : 0.0f;
                };
                return {f_normalize(x) * speed, -f_normalize(y) * speed};
            };

            ImGuiIO& io = ImGui::GetIO();

            if (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) {
                if (!b_pressed) {
                    io.MouseDown[0] = true;
                    b_pressed = true;
                }
            } else if (b_pressed) {
                io.MouseDown[0] = false;
                b_pressed = false;
            }

            io.MousePos += f_speed(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY);
        }

        return true;
    }
}
